//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

// WingBodyFillet.cpp: Rolling ball fillets between wings and fuselages.
//
// A wing whose root is buried in a fuselage gets a run of slim sections inserted near the
// junction.  Every one of them carries the wing's own airfoil -- same type, same parameters --
// and the fillet is made only from what each section can do to that airfoil: grow it (Scale),
// pull its LE forward (DeltaX), thicken it (T/C), lift or drop it (DeltaY) and pitch it (Theta).
// Lofted together, the sections swell into the fuselage like a rolling ball fillet.
//
// The target shape comes from a rolling ball fillet solved separately along each chordwise line
// of the wing.  Where that line leaves the fuselage, a plane is set up from the wing's outboard
// tangent and its surface normal.  In that plane a ball of radius R resting on the wing is rolled
// inboard until it meets the fuselage; its arc between the two contact points is the fillet.  The
// fuselage is only ever queried through true 3D distance to its surface, so the fillet follows its
// actual, doubly curved shape, and the junction itself varies along the chord as the fuselage does.
// Each section's airfoil controls are then fitted to that target by least squares.
//
//////////////////////////////////////////////////////////////////////

#include "WingBodyFillet.h"

#include "Airfoil.h"
#include "APIDefines.h"
#include "AttributeManager.h"
#include "BndBox.h"
#include "ParmMgr.h"
#include "Vehicle.h"
#include "WingGeom.h"
#include "XSecSurf.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <map>

namespace
{

// Attribute marking a fillet section, so a later run can find and remove it.
const char* FILLET_TAG = "WingBodyFillet";

bool IsFilletSect( WingSect* ws )
{
    return ws && ws->GetAttrCollection()->FindPtr( FILLET_TAG ) != nullptr;
}

void TagFilletSect( WingSect* ws )
{
    AttributeMgr.AddAttributeBool( ws->GetAttrCollection()->GetID(), FILLET_TAG, 1, false );
}

// Cosine spaced parameters across one airfoil, t in [0, 4]: lower surface TE -> LE over [0, 2],
// upper surface LE -> TE over [2, 4].  Clustered at both LE and TE.  Index nhalf is exactly the LE.
vector< double > AirfoilParams( int nhalf )
{
    vector< double > t( 2 * nhalf + 1 );
    for ( int i = 0; i <= nhalf; i++ )
    {
        double a = 1.0 - cos( M_PI * i / nhalf );
        t[i] = a;
        t[2 * nhalf - i] = 4.0 - a;
    }
    t[nhalf] = 2.0;
    return t;
}

// Piecewise linear interpolation of y( x ), x ascending.
double Interp( const vector< double > & x, const vector< double > & y, double xq )
{
    if ( xq <= x.front() )
    {
        return y.front();
    }
    if ( xq >= x.back() )
    {
        return y.back();
    }
    int i = (int)( std::upper_bound( x.begin(), x.end(), xq ) - x.begin() ) - 1;
    double f = ( xq - x[i] ) / ( x[i + 1] - x[i] );
    return y[i] + f * ( y[i + 1] - y[i] );
}

//==== Signed distance to a Geom's surfaces, negative inside ====//
class SurfDist
{
public:
    explicit SurfDist( Geom* geom )
    {
        for ( int i = 0; i < geom->GetNumTotalSurfs(); i++ )
        {
            const VspSurf* surf = geom->GetSurfPtr( i );
            if ( !surf )
            {
                continue;
            }

            // Fix the sense of the normals by looking down on the surface from well above it.
            BndBox bb;
            surf->GetBoundingBox( bb );
            vec3d above = bb.GetCenter() + vec3d( 0.0, 0.0, 2.0 * bb.DiagDist() + 1.0 );
            double u, w;
            surf->FindNearest( u, w, above );
            double side = dot( above - surf->CompPnt( u, w ), surf->CompNorm( u, w ) );

            m_Surfs.push_back( surf );
            m_Orient.push_back( side < 0.0 ? -1.0 : 1.0 );
        }
        m_U.resize( m_Surfs.size() );
        m_W.resize( m_Surfs.size() );
        m_D.resize( m_Surfs.size() );
        m_P.resize( m_Surfs.size() );
        Reset();
    }

    bool Empty() const      { return m_Surfs.empty(); }

    // Forget the last answer, so the next query does a global search.  Call before jumping
    // somewhere unrelated to the previous query.
    void Reset()
    {
        m_Warm.assign( m_Surfs.size(), false );
    }

    double operator()( const vec3d & p )
    {
        double best = DBL_MAX;
        double sd = DBL_MAX;
        for ( size_t i = 0; i < m_Surfs.size(); i++ )
        {
            const VspSurf* surf = m_Surfs[i];
            double u, w, d;
            if ( m_Warm[i] )
            {
                d = surf->FindNearest( u, w, p, m_U[i], m_W[i] );
                // Distance to a surface is 1-Lipschitz.  A local solve landing farther away than
                // this bound has found the wrong minimum, so search globally.
                if ( d > m_D[i] + dist( p, m_P[i] ) + 1.0e-9 )
                {
                    d = surf->FindNearest( u, w, p );
                }
            }
            else
            {
                d = surf->FindNearest( u, w, p );
            }

            m_U[i] = u;
            m_W[i] = w;
            m_D[i] = d;
            m_P[i] = p;
            m_Warm[i] = true;

            if ( d < best )
            {
                best = d;
                double side = dot( p - surf->CompPnt( u, w ), surf->CompNorm( u, w ) ) * m_Orient[i];
                sd = side < 0.0 ? -d : d;
            }
        }
        return sd;
    }

private:
    vector< const VspSurf* > m_Surfs;
    vector< double > m_Orient;
    vector< bool > m_Warm;
    vector< double > m_U, m_W, m_D;
    vector< vec3d > m_P;
};

//==== Evaluates the wing as built: ruled between its sections, in world coordinates ====//
// u is the section index (0 at the root), t the airfoil parameter in [0, 4].
class WingEval
{
public:
    explicit WingEval( WingGeom* wing ) : m_Foil( wing->GetFoilSurf() )
    {
        for ( int i = 0; i < wing->NumXSec(); i++ )
        {
            m_Crv.push_back( wing->GetWingSect( i )->GetCurve() );
        }

        vector< Matrix4d > xf = wing->GetTransMatVec();
        if ( !xf.empty() )
        {
            m_Xform = xf[0];
        }

        // The 2D outward normal is ( -dy, dx ) when the airfoil runs clockwise.
        vector< double > t = AirfoilParams( 50 );
        double area = 0.0;
        for ( size_t i = 0; i + 1 < t.size(); i++ )
        {
            vec3d a = Foil( 0.0, t[i] );
            vec3d b = Foil( 0.0, t[i + 1] );
            area += a.x() * b.y() - b.x() * a.y();
        }
        m_Orient2D = area > 0.0 ? -1.0 : 1.0;
    }

    double UMax() const     { return (double)m_Crv.size() - 1.0; }

    vec3d Pnt( double u, double t )
    {
        int np = (int)m_Crv.size() - 1;
        int p = std::min( std::max( (int)std::floor( u ), 0 ), np - 1 );
        double f = u - p;
        vec3d a = m_Crv[p].CompPnt( t );
        vec3d b = m_Crv[p + 1].CompPnt( t );
        return m_Xform.xform( a + ( b - a ) * f );
    }

    // Unit chord airfoil at u, in its own 2D frame.
    vec3d Foil( double u, double t ) const
    {
        return m_Foil.CompPnt( u, t );
    }

    // Outward unit normal of the unit chord airfoil at u, in its own 2D frame.
    vec3d FoilNormal( double u, double t ) const
    {
        double dt = 1.0e-4;
        double t0 = std::max( t - dt, 0.0 );
        double t1 = std::min( t + dt, 4.0 );
        vec3d d = Foil( u, t1 ) - Foil( u, t0 );
        vec3d n( -d.y() * m_Orient2D, d.x() * m_Orient2D, 0.0 );
        n.normalize();
        return n;
    }

    double Chord( double u )
    {
        return dist( Pnt( u, 0.0 ), Pnt( u, 2.0 ) );
    }

    // At ( u, t ): the outward surface normal n, the outboard tangent s made normal to n, and
    // g = |dP/du|, which converts distance along s into u.
    void Frame( double u, double t, vec3d & n, vec3d & s, double & g )
    {
        double du = 1.0e-4;
        double u0 = std::max( u - du, 0.0 );
        double u1 = std::min( u + du, UMax() );
        vec3d dpdu = ( Pnt( u1, t ) - Pnt( u0, t ) ) / ( u1 - u0 );

        double dt = 1.0e-4;
        double t0 = std::max( t - dt, 0.0 );
        double t1 = std::min( t + dt, 4.0 );
        vec3d dpdt = ( Pnt( u, t1 ) - Pnt( u, t0 ) ) / ( t1 - t0 );

        n = cross( dpdt, dpdu );
        n.normalize();

        // Settle the sign of n by carrying the 2D outward normal into 3D.  The 2D chord axis
        // maps to LE -> TE, and the 2D thickness axis to the side the upper surface lies on.
        vec3d c3 = Pnt( u, 0.0 ) - Pnt( u, 2.0 );
        c3.normalize();
        if ( Foil( u, 0.0 ).x() < Foil( u, 2.0 ).x() )
        {
            c3 = c3 * -1.0;
        }
        vec3d h3 = cross( dpdu, c3 );
        h3.normalize();
        double up3 = dot( Pnt( u, 3.0 ) - Pnt( u, 1.0 ), h3 );
        double up2 = Foil( u, 3.0 ).y() - Foil( u, 1.0 ).y();
        if ( up3 * up2 < 0.0 )
        {
            h3 = h3 * -1.0;
        }
        vec3d n2 = FoilNormal( u, t );
        vec3d est = c3 * n2.x() + h3 * n2.y();
        est.normalize();
        if ( n.mag() < 0.5 )
        {
            // Degenerate surface tangents, as at the LE and TE: carried over 2D normal will do.
            n = est;
        }
        else if ( dot( n, est ) < 0.0 )
        {
            n = n * -1.0;
        }

        g = dpdu.mag();
        s = dpdu - n * dot( dpdu, n );
        s.normalize();
    }

private:
    vector< VspCurve > m_Crv;
    const VspSurf & m_Foil;
    Matrix4d m_Xform;
    double m_Orient2D = 1.0;
};

//==== Fillet cross-section along one chordwise line of the wing ====//
// Lives in the plane through C spanned by S (outboard along the wing) and N (wing normal).
// m_H[i] is the height of the fillet above the wing at distance m_SGrid[i] outboard of C.
struct Profile
{
    bool m_Valid = false;
    double m_UStar = 0.0;       // Where this chord line leaves the fuselage.
    vec3d m_C, m_S, m_N;
    double m_G = 1.0;
    double m_S0 = 0.0;          // Inboard of this the fillet is buried in the fuselage.
    double m_SEnd = 0.0;        // Outboard of this the fillet has run out onto the wing.
    vector< double > m_SGrid;
    vector< double > m_H;

    double Height( double s ) const
    {
        if ( !m_Valid || s >= m_SEnd )
        {
            return 0.0;
        }
        return Interp( m_SGrid, m_H, std::max( s, m_S0 ) );
    }
};

// Find where the chord line at t leaves the fuselage, walking out from the root.
bool FindExit( WingEval & wing, SurfDist & fuse, double t, double & ustar )
{
    fuse.Reset();
    if ( fuse( wing.Pnt( 0.0, t ) ) >= 0.0 )
    {
        return false;   // Root is not buried here.
    }

    const double du = 0.01;
    double ulo = 0.0;
    for ( double u = du; u <= wing.UMax() + 1.0e-12; u += du )
    {
        if ( fuse( wing.Pnt( u, t ) ) >= 0.0 )
        {
            double uhi = u;
            for ( int k = 0; k < 40; k++ )
            {
                double um = 0.5 * ( ulo + uhi );
                if ( fuse( wing.Pnt( um, t ) ) >= 0.0 )
                {
                    uhi = um;
                }
                else
                {
                    ulo = um;
                }
            }
            ustar = 0.5 * ( ulo + uhi );
            return true;
        }
        ulo = u;
    }
    return false;
}

// Does a ball of radius R resting on the wing, centred at distance a outboard, clear the fuselage?
bool BallClears( SurfDist & fuse, const Profile & pr, double a, double R )
{
    return fuse( pr.m_C + pr.m_S * a + pr.m_N * R ) >= R * ( 1.0 - 1.0e-6 );
}

// Distance outboard of the most inboard ball of radius R that rests on the wing and clears the
// fuselage.  That ball touches both, so its lower arc is the fillet.
bool FindBall( SurfDist & fuse, const Profile & pr, double R, double & astar )
{
    const double step = 0.25 * R;
    const double amax = 20.0 * R;

    fuse.Reset();
    double a_in, a_out;
    if ( BallClears( fuse, pr, 0.0, R ) )
    {
        // The fuselage falls away from the wing here: walk inboard until the ball hits it.
        a_out = 0.0;
        a_in = -step;
        while ( BallClears( fuse, pr, a_in, R ) )
        {
            a_out = a_in;
            a_in -= step;
            if ( a_in < -amax )
            {
                return false;
            }
        }
    }
    else
    {
        a_in = 0.0;
        a_out = step;
        while ( !BallClears( fuse, pr, a_out, R ) )
        {
            a_in = a_out;
            a_out += step;
            if ( a_out > amax )
            {
                return false;
            }
        }
    }

    for ( int k = 0; k < 30; k++ )
    {
        double am = 0.5 * ( a_in + a_out );
        if ( BallClears( fuse, pr, am, R ) )
        {
            a_out = am;
        }
        else
        {
            a_in = am;
        }
    }
    astar = a_out;
    return true;
}

void SolveProfile( SurfDist & fuse, double R, Profile & pr )
{
    double astar;
    if ( !FindBall( fuse, pr, R, astar ) )
    {
        pr.m_Valid = false;
        return;
    }

    // Lower arc of the ball, from where it rests on the wing inboard to its leftmost point.
    const int narc = 48;
    vector< double > s( narc + 1 ), h( narc + 1 ), gap( narc + 1 );
    fuse.Reset();
    for ( int i = 0; i <= narc; i++ )
    {
        // Spaced evenly in angle, so the steep inboard end is resolved.
        double th = 0.5 * M_PI * i / narc;
        s[i] = astar - R * sin( th );
        h[i] = R * ( 1.0 - cos( th ) );
        gap[i] = fuse( pr.m_C + pr.m_S * s[i] + pr.m_N * h[i] );
    }

    // The arc touches the fuselage where it comes closest to it.  The ball only clears the
    // fuselage in 3D, so in this plane the arc can pass a hair away rather than touch.
    int icon = (int)( std::min_element( gap.begin(), gap.end() ) - gap.begin() );
    double s0 = s[icon];
    double h0 = h[icon];

    if ( icon == narc && gap[narc] > 0.05 * R )
    {
        // Undercut corner: the fuselage overhangs the wing and the ball touches it above its
        // centre.  Fill straight up from the arc's leftmost point to the fuselage rather than
        // leave a crevice under it -- to where it meets the fuselage, or comes closest to it
        // when this line just grazes the fuselage's widest point.  Kept modest; a section can
        // only swell so far before it stops looking like the wing's airfoil.
        const double hcap = 1.5 * R;
        const double dh = 0.05 * R;
        vec3d base = pr.m_C + pr.m_S * s0;
        double hbest = h0;
        double gbest = gap[narc];
        for ( double hh = h0 + dh; hh <= hcap; hh += dh )
        {
            double g = fuse( base + pr.m_N * hh );
            if ( g < gbest )
            {
                gbest = g;
                hbest = hh;
            }
            if ( g <= 0.01 * R || g > gbest + 0.1 * R )
            {
                break;  // Touched, or clearly moving away again.
            }
        }
        h0 = hbest;
        s.push_back( s0 - 1.0e-3 * R );
        h.push_back( h0 );
        icon = (int)s.size() - 1;
        s0 = s[icon];
    }

    // Stored ascending in s.  Inboard of s0 the fillet is held level, inside the fuselage.
    pr.m_SGrid.clear();
    pr.m_H.clear();
    for ( int i = icon; i >= 0; i-- )
    {
        pr.m_SGrid.push_back( s[i] );
        pr.m_H.push_back( h[i] );
    }
    pr.m_S0 = s0;
    pr.m_SEnd = astar;
}

//==== One surface of an airfoil as y( x ), LE -> TE ====//
class FoilHalf
{
public:
    // pts run LE -> TE.  Points that double back in x (as an offset LE can) are dropped.
    explicit FoilHalf( const vector< vec3d > & pts )
    {
        for ( const vec3d & p : pts )
        {
            if ( m_X.empty() || p.x() > m_X.back() + 1.0e-9 )
            {
                m_X.push_back( p.x() );
                m_Y.push_back( p.y() );
            }
        }
    }

    double Y( double x ) const  { return Interp( m_X, m_Y, x ); }

private:
    vector< double > m_X, m_Y;
};

//==== What a fillet section does to its airfoil ====//
// The airfoil is scaled by Scale and pitched by Theta, both about its TE, which therefore stays
// exactly where it was -- the wing's TE runs on unbroken through the fillet.  Its thickness/chord
// is multiplied by ThickRatio.  All in the unit chord frame of the section's own airfoil.
struct SectFit
{
    double m_Scale = 1.0;
    double m_Theta = 0.0;       // Degrees, positive drops the LE.
    double m_ThickRatio = 1.0;
    double m_TEx = 1.0;         // The TE the scaling and pitching are about.
    double m_TEy = 0.0;
};

// Least squares fit of the airfoil controls to a target airfoil.  Both are sampled at the same
// parameters tm (from AirfoilParams( mhalf )), in the unit chord frame of the original airfoil.
SectFit FitSection( const vector< vec3d > & foil, const vector< vec3d > & target, int mhalf )
{
    SectFit fit;

    auto halves = []( const vector< vec3d > & loop, int mh, vector< vec3d > & lo, vector< vec3d > & up )
    {
        for ( int i = mh; i >= 0; i-- )
        {
            lo.push_back( loop[i] );
        }
        for ( int i = mh; i <= 2 * mh; i++ )
        {
            up.push_back( loop[i] );
        }
    };
    vector< vec3d > flo, fup, tlo, tup;
    halves( foil, mhalf, flo, fup );
    halves( target, mhalf, tlo, tup );
    FoilHalf fl( flo ), fu( fup ), tl( tlo ), tu( tup );

    const vec3d le = foil[mhalf];
    const double te_x = 0.5 * ( foil.front().x() + foil.back().x() );
    const double te_y = 0.5 * ( foil.front().y() + foil.back().y() );
    const double c0 = te_x - le.x();
    fit.m_TEx = te_x;
    fit.m_TEy = te_y;
    if ( c0 <= 0.0 )
    {
        return fit;
    }

    // The LE comes forward as far as the fillet pushes it; the TE stays put.
    double dle = 0.0;
    for ( size_t i = 0; i < foil.size(); i++ )
    {
        dle = std::min( dle, target[i].x() - foil[i].x() );
    }
    const double S = ( c0 - dle ) / c0;

    // At matching chord fractions, compare camber and half thickness.  Scaled about the TE, the
    // old camber line still needs pitching by theta to match; the old half thickness, scaling
    // by k.  For small theta, pitching about the TE lifts a point w ahead of it by -theta * w.
    const int n = 40;
    double sww = 0, swr = 0, st_num = 0, st_den = 0;
    for ( int i = 0; i < n; i++ )
    {
        double xi = 0.04 + 0.86 * i / ( n - 1.0 );
        double xo = le.x() + xi * c0;
        double xn = te_x + S * ( xo - te_x );

        double yc0 = 0.5 * ( fu.Y( xo ) + fl.Y( xo ) );
        double yt0 = 0.5 * ( fu.Y( xo ) - fl.Y( xo ) );
        double tc = 0.5 * ( tu.Y( xn ) + tl.Y( xn ) );
        double tt = 0.5 * ( tu.Y( xn ) - tl.Y( xn ) );

        double w = xn - te_x;
        double r = tc - te_y - S * ( yc0 - te_y );
        sww += w * w;
        swr += w * r;

        st_num += tt * S * yt0;
        st_den += S * yt0 * S * yt0;
    }

    fit.m_Scale = S;
    fit.m_Theta = sww > 0.0 ? atan( swr / sww ) * 180.0 / M_PI : 0.0;
    fit.m_ThickRatio = st_den > 0.0 ? std::min( std::max( st_num / st_den, 1.0 ), 4.0 ) : 1.0;
    return fit;
}

// The fits come from one station at a time.  Make them into one smooth fillet: it may only
// grow toward the fuselage, and the pitch is smoothed out along the span.  The innermost
// station (buried) and the outermost (where the fillet has run out) are left as they are.
void SmoothFits( vector< SectFit > & fits )
{
    const int n = (int)fits.size();
    for ( int k = n - 2; k >= 0; k-- )
    {
        fits[k].m_Scale = std::max( fits[k].m_Scale, fits[k + 1].m_Scale );
        fits[k].m_ThickRatio = std::max( fits[k].m_ThickRatio, fits[k + 1].m_ThickRatio );
    }

    const double max_theta = 6.0;
    for ( SectFit & f : fits )
    {
        f.m_Theta = std::min( std::max( f.m_Theta, -max_theta ), max_theta );
    }
    for ( int pass = 0; pass < 3; pass++ )
    {
        vector< SectFit > prev = fits;
        for ( int k = 1; k < n - 1; k++ )
        {
            fits[k].m_Theta = 0.25 * prev[k - 1].m_Theta + 0.5 * prev[k].m_Theta + 0.25 * prev[k + 1].m_Theta;
            fits[k].m_Scale = 0.25 * prev[k - 1].m_Scale + 0.5 * prev[k].m_Scale + 0.25 * prev[k + 1].m_Scale;
            fits[k].m_ThickRatio = 0.25 * prev[k - 1].m_ThickRatio + 0.5 * prev[k].m_ThickRatio + 0.25 * prev[k + 1].m_ThickRatio;
        }
    }
}

// Set the airfoil of 'dst' to that of the sections either side, blended to fraction f along the
// panel between them.  Same airfoil type both sides: its numbers are blended.  Otherwise the
// nearer side's airfoil is taken as is.
void BlendAirfoil( WingSect* dst, WingSect* in, WingSect* out, double f )
{
    XSecCurve* dc = dst->GetXSecCurve();
    XSecCurve* ic = in->GetXSecCurve();
    XSecCurve* oc = out->GetXSecCurve();
    if ( !dc || !ic || !oc || ic->GetType() != oc->GetType() || dc->GetType() != ic->GetType() )
    {
        return;
    }

    vector< string > ids;
    dc->AddLinkableParms( ids );
    for ( const string & id : ids )
    {
        Parm* p = ParmMgr.FindParm( id );
        if ( !p || p->GetType() != vsp::PARM_DOUBLE_TYPE )
        {
            continue;
        }
        Parm* pi = ParmMgr.FindParm( ic->FindParm( p->GetName(), p->GetGroupName() ) );
        Parm* po = ParmMgr.FindParm( oc->FindParm( p->GetName(), p->GetGroupName() ) );
        if ( pi && po )
        {
            p->Set( pi->Get() + f * ( po->Get() - pi->Get() ) );
        }
    }
}

// Apply a fit on top of whatever the airfoil's own Scale / Delta / Theta already were.
void ApplyFit( WingSect* ws, const SectFit & fit )
{
    XSecCurve* xsc = ws->GetXSecCurve();

    // Scaling and pitching about the TE is Rot( th ) * S * old + D, with D = TE - Rot( th ) * S * TE.
    // On top of the airfoil's own old = Rot( th0 ) * S0 * A + D0 that is
    //     Rot( th + th0 ) * S * S0 * A + Rot( th ) * S * D0 + D
    // XSecCurve's rotation, scale and shift are in that order, about the airfoil's origin.
    double th = fit.m_Theta * M_PI / 180.0;
    double ct = cos( th ), st = sin( th );
    double S = fit.m_Scale;
    double dx = fit.m_TEx - S * ( ct * fit.m_TEx - st * fit.m_TEy );
    double dy = fit.m_TEy - S * ( st * fit.m_TEx + ct * fit.m_TEy );

    double dx0 = xsc->m_DeltaX();
    double dy0 = xsc->m_DeltaY();

    xsc->m_Theta = xsc->m_Theta() + fit.m_Theta;
    xsc->m_Scale = xsc->m_Scale() * S;
    xsc->m_DeltaX = S * ( ct * dx0 - st * dy0 ) + dx;
    xsc->m_DeltaY = S * ( st * dx0 + ct * dy0 ) + dy;

    Airfoil* af = dynamic_cast< Airfoil* >( xsc );
    if ( af )
    {
        af->m_ThickChord = af->m_ThickChord() * fit.m_ThickRatio;
    }
}

//==== Split panel 'tip' at fractions fr, giving the new sections the fillets fits ====//
// The planform is untouched: the new sections sit on the panel's own straight LE and TE, with
// its chord, sweep, dihedral and twist.  Only their airfoils are reshaped.
void SplitPanel( WingGeom* wing, int tip, const vector< double > & fr, const vector< SectFit > & fits )
{
    XSecSurf* xss = wing->GetXSecSurf( 0 );
    WingSect* tws = wing->GetWingSect( tip );
    WingSect* rws = wing->GetWingSect( tip - 1 );
    if ( !xss || !tws || !rws || fr.empty() )
    {
        return;
    }

    const double span = tws->m_Span();
    const double rc = tws->m_RootChord();
    const double tc = tws->m_TipChord();
    const double dihed = tws->m_Dihedral();
    const double twist = tws->m_Twist();
    const double twist_in = rws->m_Twist();
    const bool reldihed = wing->m_RelativeDihedralFlag();
    const bool reltwist = wing->m_RelativeTwistFlag();

    // Each new section starts as a copy of the nearer end of the panel, airfoil type and all.
    // Sections tip .. tip+m-1 are new; the original tip section ends up at tip+m.
    const int m = (int)fr.size();
    for ( int i = 0; i < m; i++ )
    {
        WingSect* src = fr[i] < 0.5 ? rws : tws;
        xss->InsertXSec( src->GetXSecCurve()->GetType(), tip - 1 + i );
        WingSect* ws = wing->GetWingSect( tip + i );
        ws->CopyFrom( src );
        BlendAirfoil( ws, rws, tws, fr[i] );
        TagFilletSect( ws );
    }

    // A panel is straight, so sweep at a given chord fraction is the same along all of it.
    for ( int i = 0; i <= m; i++ )
    {
        WingSect* ws = wing->GetWingSect( tip + i );
        double f0 = ( i == 0 ) ? 0.0 : fr[i - 1];
        double f1 = ( i == m ) ? 1.0 : fr[i];

        if ( i < m )
        {
            // These two may not be equal, so whichever goes first could clash with the other's
            // old value.
            ws->m_SweepLoc.Set( tws->m_SweepLoc() );
            ws->m_SecSweepLoc.Set( tws->m_SecSweepLoc() );
            ws->m_SweepLoc.Set( tws->m_SweepLoc() );
            ws->m_Sweep = tws->m_Sweep();
            ws->m_TwistLoc = tws->m_TwistLoc();
            ws->m_RotateMatchDiedralFlag = tws->m_RotateMatchDiedralFlag();
            ws->m_TwistInXZPlaneFlag = tws->m_TwistInXZPlaneFlag();
        }
        ws->m_Dihedral = ( reldihed && i > 0 ) ? 0.0 : dihed;
        ws->m_Twist = reltwist ? twist * ( f1 - f0 ) : twist_in + ( twist - twist_in ) * f1;
        ws->ForceSpanRcTc( span * ( f1 - f0 ), rc + ( tc - rc ) * f0, rc + ( tc - rc ) * f1 );

        if ( i < m )
        {
            ApplyFit( ws, fits[i] );
        }
    }
}

// The blend angles ( theta, phi ) at airfoil parameter t of a section that point its blend
// tangent along d.  Solved numerically, as XSec only gives the forward map.
bool SolveTangentAngles( WingSect* ws, double t, const vec3d & d, double & th, double & ph )
{
    vec3d tan, nrm, ta, tb;
    th = 0.0;
    ph = 0.0;
    ws->GetTanNormVec( t, th, ph, tan, nrm );
    if ( dot( tan, d ) < 0.0 )
    {
        th = M_PI;
    }

    for ( int it = 0; it < 40; it++ )
    {
        ws->GetTanNormVec( t, th, ph, tan, nrm );
        vec3d r = tan - d;
        if ( r.mag() < 1.0e-10 )
        {
            break;
        }

        const double h = 1.0e-6;
        ws->GetTanNormVec( t, th + h, ph, ta, nrm );
        ws->GetTanNormVec( t, th, ph + h, tb, nrm );
        vec3d ja = ( ta - tan ) / h;
        vec3d jb = ( tb - tan ) / h;

        double a11 = dot( ja, ja ), a12 = dot( ja, jb ), a22 = dot( jb, jb );
        double b1 = -dot( ja, r ), b2 = -dot( jb, r );
        double det = a11 * a22 - a12 * a12;
        if ( fabs( det ) < 1.0e-14 )
        {
            return false;
        }
        double dth = ( b1 * a22 - b2 * a12 ) / det;
        double dph = ( a11 * b2 - a12 * b1 ) / det;
        double step = std::max( fabs( dth ), fabs( dph ) );
        if ( step > 0.3 )
        {
            dth *= 0.3 / step;
            dph *= 0.3 / step;
        }
        th += dth;
        ph += dph;
    }

    ws->GetTanNormVec( t, th, ph, tan, nrm );
    return dot( tan, d ) > 0.9999;
}

// Run the LE through the fillet sections as one smooth curve instead of straight segments: each
// fillet section blends its LE along the line from its inboard to its outboard neighbor.  Where
// the fillet runs out into the plain wing, it leaves along the plain LE, which stays straight.
// The TE needs nothing; the fillet never moves it off the wing's own TE line.
void SmoothLE( WingGeom* wing )
{
    const int n = wing->NumXSec();
    vector< vec3d > le( n );
    for ( int i = 0; i < n; i++ )
    {
        le[i] = wing->GetWingSect( i )->GetCurve().CompPnt( 2.0 );
    }

    for ( int i = 1; i < n - 1; i++ )
    {
        WingSect* ws = wing->GetWingSect( i );
        if ( !IsFilletSect( ws ) )
        {
            continue;
        }

        vec3d d = IsFilletSect( wing->GetWingSect( i + 1 ) ) ? le[i + 1] - le[i - 1] : le[i + 1] - le[i];
        d.normalize();

        double th, ph;
        if ( !SolveTangentAngles( ws, 2.0, d, th, ph ) )
        {
            continue;
        }

        // GetJoints feeds the inboard side ( -sweep, -dihedral ) and the outboard ( -sweep, +dihedral ).
        ws->m_InLEMode = vsp::BLEND_ANGLES;
        ws->m_OutLEMode = vsp::BLEND_ANGLES;
        ws->m_InLESweep = -th * 180.0 / M_PI;
        ws->m_InLEDihedral = -ph * 180.0 / M_PI;
        ws->m_OutLESweep = -th * 180.0 / M_PI;
        ws->m_OutLEDihedral = ph * 180.0 / M_PI;
        ws->m_InLEStrength = 1.0;
        ws->m_OutLEStrength = 1.0;
    }

    wing->m_SurfDirty = true;
    wing->Update();
}

// Fraction of the wing's root chord points buried in geom.
double BuriedFraction( WingEval & wing, SurfDist & body )
{
    vector< double > t = AirfoilParams( 8 );
    int nin = 0;
    for ( double ti : t )
    {
        body.Reset();
        if ( body( wing.Pnt( 0.0, ti ) ) < 0.0 )
        {
            nin++;
        }
    }
    return (double)nin / t.size();
}

bool FilletWing( WingGeom* wing, Geom* fuse, const WingBodyFillet::Settings & settings, string & msg )
{
    WingEval we( wing );
    SurfDist fd( fuse );

    if ( we.UMax() < 1.0 || fd.Empty() )
    {
        msg = "nothing to fillet";
        return false;
    }

    //==== Where each chord line leaves the fuselage ====//
    vector< double > tj = AirfoilParams( 20 );
    vector< Profile > prof( tj.size() );
    int nvalid = 0;
    for ( size_t j = 0; j < tj.size(); j++ )
    {
        prof[j].m_Valid = FindExit( we, fd, tj[j], prof[j].m_UStar );
        nvalid += prof[j].m_Valid ? 1 : 0;
    }
    if ( nvalid < (int)tj.size() / 2 )
    {
        msg = "root is not inside the fuselage";
        return false;
    }

    // Radius from the chord where the wing comes out of the fuselage.
    vector< double > ustars;
    for ( const Profile & p : prof )
    {
        if ( p.m_Valid )
        {
            ustars.push_back( p.m_UStar );
        }
    }
    std::sort( ustars.begin(), ustars.end() );
    const double R = settings.m_Radius > 0.0 ? settings.m_Radius :
                     settings.m_RadiusFrac * we.Chord( ustars[ustars.size() / 2] );
    if ( R <= 0.0 )
    {
        msg = "zero fillet radius";
        return false;
    }

    //==== Fillet cross-section along each chord line ====//
    for ( size_t j = 0; j < tj.size(); j++ )
    {
        Profile & p = prof[j];
        if ( !p.m_Valid )
        {
            continue;
        }
        p.m_C = we.Pnt( p.m_UStar, tj[j] );
        we.Frame( p.m_UStar, tj[j], p.m_N, p.m_S, p.m_G );
        SolveProfile( fd, R, p );
    }

    //==== Span range the fillet covers ====//
    double u0 = DBL_MAX, u1 = -DBL_MAX;
    for ( const Profile & p : prof )
    {
        if ( p.m_Valid )
        {
            u0 = std::min( u0, p.m_UStar + p.m_S0 / p.m_G );
            u1 = std::max( u1, p.m_UStar + p.m_SEnd / p.m_G );
        }
    }
    // Inboard, the fillet is buried; keep its first section clear of the root.
    u0 = std::max( u0, 0.5 * ustars.front() );
    u1 = std::min( u1, we.UMax() - 1.0e-3 );
    if ( u1 <= u0 )
    {
        msg = "fillet would be too small";
        return false;
    }

    // Fillet height along every chord line at span u.
    auto heights = [&]( double u )
    {
        vector< double > hj( tj.size(), 0.0 );
        for ( size_t j = 0; j < tj.size(); j++ )
        {
            const Profile & p = prof[j];
            if ( p.m_Valid )
            {
                hj[j] = p.Height( dot( we.Pnt( u, tj[j] ) - p.m_C, p.m_S ) );
            }
        }
        return hj;
    };

    // Place the stations where the fillet changes shape.  How much it changes is summed over
    // every chord line, so a fillet that rolls up the fuselage further out on one surface than
    // the other still gets its sections where each one curves.  The first station holds the
    // fillet at full size, buried in the fuselage; the last is where it has run out.
    const int nfine = 400;
    vector< double > ufine( nfine ), cum( nfine, 0.0 );
    vector< double > hprev = heights( u0 );
    ufine[0] = u0;
    for ( int i = 1; i < nfine; i++ )
    {
        ufine[i] = u0 + ( u1 - u0 ) * i / ( nfine - 1.0 );
        vector< double > h = heights( ufine[i] );
        double var = 0.0;
        for ( size_t j = 0; j < h.size(); j++ )
        {
            var += fabs( h[j] - hprev[j] );
        }
        cum[i] = cum[i - 1] + var;
        hprev = h;
    }
    // A little even spacing too, so no stretch of the fillet goes without a section.
    const double even = 0.1 * cum.back() / ( nfine - 1.0 ) + 1.0e-12;
    for ( int i = 1; i < nfine; i++ )
    {
        cum[i] += even * i;
    }

    const int ns = std::max( settings.m_NumSect, 2 );
    vector< double > uk;
    for ( int k = 0; k < ns; k++ )
    {
        double u = Interp( cum, ufine, cum.back() * k / ( ns - 1.0 ) );

        // Stay off existing sections.
        double r = std::round( u );
        if ( fabs( u - r ) < 1.0e-3 )
        {
            u = ( u >= r || r == 0.0 ) ? r + 1.0e-3 : r - 1.0e-3;
        }
        if ( uk.empty() || u > uk.back() + 1.0e-4 )
        {
            uk.push_back( u );
        }
    }

    // Every airfoil the new sections are made from must be a real airfoil, whose unit chord frame
    // the fit works in.
    int sfirst = (int)std::floor( uk.front() );
    int slast = std::min( (int)std::floor( uk.back() ) + 1, wing->NumXSec() - 1 );
    for ( int i = sfirst; i <= slast; i++ )
    {
        if ( !dynamic_cast< Airfoil* >( wing->GetWingSect( i )->GetXSecCurve() ) )
        {
            msg = "sections near the root must be airfoils";
            return false;
        }
    }

    //==== Fit each station's airfoil controls to its fillet, from the unmodified wing ====//
    const int mhalf = 60;
    vector< double > tm = AirfoilParams( mhalf );

    vector< SectFit > fits( uk.size() );
    for ( size_t k = 0; k < uk.size(); k++ )
    {
        double u = uk[k];
        vector< double > hj = heights( u );
        double chord = we.Chord( u );
        vector< vec3d > foil( tm.size() ), target( tm.size() );
        for ( size_t i = 0; i < tm.size(); i++ )
        {
            foil[i] = we.Foil( u, tm[i] );
            target[i] = foil[i] + we.FoilNormal( u, tm[i] ) * ( Interp( tj, hj, tm[i] ) / chord );
        }
        fits[k] = FitSection( foil, target, mhalf );
    }
    SmoothFits( fits );

    //==== Insert the sections, outboard panels first so inboard indices hold ====//
    std::map< int, vector< int > > by_panel;
    for ( size_t k = 0; k < uk.size(); k++ )
    {
        by_panel[ (int)std::floor( uk[k] ) ].push_back( (int)k );
    }
    for ( auto it = by_panel.rbegin(); it != by_panel.rend(); ++it )
    {
        int panel = it->first;
        vector< double > fr;
        vector< SectFit > pfits;
        for ( int k : it->second )
        {
            fr.push_back( uk[k] - panel );
            pfits.push_back( fits[k] );
        }
        SplitPanel( wing, panel + 1, fr, pfits );
    }

    wing->m_SurfDirty = true;
    wing->Update();
    SmoothLE( wing );

    const SectFit & big = fits.front();
    char buf[160];
    snprintf( buf, sizeof( buf ), "%d sections, radius %g; innermost airfoil x%.2f chord, x%.2f thickness",
              (int)uk.size(), R, big.m_Scale, big.m_Scale * big.m_ThickRatio );
    msg = buf;
    return true;
}

} // namespace

namespace WingBodyFillet
{

bool IsFilletSection( WingSect* ws )
{
    return IsFilletSect( ws );
}

bool RemoveFillets( WingGeom* wing )
{
    XSecSurf* xss = wing->GetXSecSurf( 0 );
    if ( !xss )
    {
        return false;
    }

    const bool reldihed = wing->m_RelativeDihedralFlag();
    const bool reltwist = wing->m_RelativeTwistFlag();
    bool removed = false;

    int i = 1;
    while ( i < wing->NumXSec() )
    {
        if ( !IsFilletSect( wing->GetWingSect( i ) ) )
        {
            i++;
            continue;
        }

        // Fillet sections first .. last, ending on the original section 'tip'.
        int first = i;
        int last = i;
        while ( last + 1 < wing->NumXSec() && IsFilletSect( wing->GetWingSect( last + 1 ) ) )
        {
            last++;
        }
        int tip = last + 1;
        if ( tip >= wing->NumXSec() )
        {
            break;
        }

        double span = 0.0, dihed_sum = 0.0, twist_sum = 0.0;
        for ( int k = first; k <= tip; k++ )
        {
            WingSect* ws = wing->GetWingSect( k );
            span += ws->m_Span();
            dihed_sum += ws->m_Dihedral();
            twist_sum += ws->m_Twist();
        }
        WingSect* tws = wing->GetWingSect( tip );
        double rc = wing->GetWingSect( first )->m_RootChord();
        double tc = tws->m_TipChord();
        double dihed = reldihed ? dihed_sum : tws->m_Dihedral();
        double twist = reltwist ? twist_sum : tws->m_Twist();

        for ( int k = last; k >= first; k-- )
        {
            xss->DeleteXSec( k );
        }

        tws = wing->GetWingSect( first );
        tws->m_Dihedral = dihed;
        tws->m_Twist = twist;
        tws->ForceSpanRcTc( span, rc, tc );

        removed = true;
        i = first + 1;
    }

    if ( removed )
    {
        wing->m_SurfDirty = true;
        wing->Update();
    }
    return removed;
}

int BlendAll( Vehicle* veh, vector< string > & messages, const Settings & settings )
{
    vector< WingGeom* > wings;
    vector< Geom* > fuses;
    for ( Geom* g : veh->FindGeomVec( veh->GetGeomVec() ) )
    {
        if ( !g )
        {
            continue;
        }
        if ( g->GetType().m_Type == MS_WING_GEOM_TYPE )
        {
            WingGeom* wg = dynamic_cast< WingGeom* >( g );
            if ( wg )
            {
                wings.push_back( wg );
            }
        }
        else if ( g->GetType().m_Type == FUSELAGE_GEOM_TYPE )
        {
            fuses.push_back( g );
        }
    }

    if ( fuses.empty() )
    {
        messages.push_back( "No fuselages to blend into." );
        return 0;
    }

    int nblend = 0;
    for ( WingGeom* wing : wings )
    {
        RemoveFillets( wing );

        // Blend into whichever fuselage buries most of the wing's root.
        Geom* best = nullptr;
        double best_frac = 0.0;
        {
            WingEval we( wing );
            for ( Geom* fuse : fuses )
            {
                SurfDist fd( fuse );
                if ( fd.Empty() )
                {
                    continue;
                }
                double frac = BuriedFraction( we, fd );
                if ( frac > best_frac )
                {
                    best_frac = frac;
                    best = fuse;
                }
            }
        }

        string msg;
        if ( !best )
        {
            msg = "root is not inside any fuselage";
        }
        else if ( FilletWing( wing, best, settings, msg ) )
        {
            msg = "blended into " + best->GetName() + " (" + msg + ")";
            nblend++;
        }
        messages.push_back( wing->GetName() + ": " + msg );
    }

    veh->Update();
    return nblend;
}

} // namespace WingBodyFillet
