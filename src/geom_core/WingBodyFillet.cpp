//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

// WingBodyFillet.cpp: Rolling ball fillets between wings and fuselages.
//
// A wing whose root is buried in a fuselage gets a run of slim sections inserted near the
// junction.  Each one carries the wing's own airfoil at that station, pushed out along its
// normal by the local height of the fillet.
//
// The fillet is solved separately along each chordwise line of the wing.  Where that line leaves
// the fuselage, a plane is set up from the wing's outboard tangent and its surface normal.  In
// that plane a ball of radius R resting on the wing is rolled inboard until it meets the
// fuselage; its arc between the two contact points is the fillet.  The fuselage is only ever
// queried through true 3D distance to its surface, so the fillet follows its actual, doubly
// curved shape, and the intersection itself varies along the chord as the fuselage does.
//
//////////////////////////////////////////////////////////////////////

#include "WingBodyFillet.h"

#include "Airfoil.h"
#include "APIDefines.h"
#include "BndBox.h"
#include "Vehicle.h"
#include "WingGeom.h"
#include "XSecSurf.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <map>

namespace
{

// Name given to the airfoil of every fillet section, so they can be found and removed again.
const char* FILLET_TAG = "VSP_WingBodyFillet";

bool IsFilletSect( WingSect* ws )
{
    if ( !ws )
    {
        return false;
    }
    FileAirfoil* fa = dynamic_cast< FileAirfoil* >( ws->GetXSecCurve() );
    return fa && fa->GetAirfoilName() == FILLET_TAG;
}

double SmoothStep( double x )
{
    x = std::min( std::max( x, 0.0 ), 1.0 );
    return x * x * ( 3.0 - 2.0 * x );
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
        // when this line just grazes the fuselage's widest point.
        const double hcap = 4.0 * R;
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

//==== Split panel 'tip' at fractions fr, giving the new sections the airfoils up / low ====//
void SplitPanel( WingGeom* wing, int tip, const vector< double > & fr,
                 const vector< vector< vec3d > > & up, const vector< vector< vec3d > > & low )
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
    const double sweep = tws->m_Sweep();
    const double sweeploc = tws->m_SweepLoc();
    const double secsweeploc = tws->m_SecSweepLoc();
    const double dihed = tws->m_Dihedral();
    const double twist = tws->m_Twist();
    const double twistloc = tws->m_TwistLoc();
    const double twist_in = rws->m_Twist();
    const bool rotmatch = tws->m_RotateMatchDiedralFlag();
    const bool twistxz = tws->m_TwistInXZPlaneFlag();
    const bool reldihed = wing->m_RelativeDihedralFlag();
    const bool reltwist = wing->m_RelativeTwistFlag();

    const int m = (int)fr.size();
    for ( int i = 0; i < m; i++ )
    {
        xss->InsertXSec( vsp::XS_FILE_AIRFOIL, tip - 1 + i );
    }

    // Sections tip .. tip+m-1 are new; the original tip section is now tip+m.  A panel is
    // straight, so sweep at a given chord fraction is the same along all of it.
    for ( int i = 0; i <= m; i++ )
    {
        WingSect* ws = wing->GetWingSect( tip + i );
        double f0 = ( i == 0 ) ? 0.0 : fr[i - 1];
        double f1 = ( i == m ) ? 1.0 : fr[i];

        ws->m_Sweep = sweep;
        // These two may not be equal, so whichever goes first could clash with the other's old value.
        ws->m_SweepLoc.Set( sweeploc );
        ws->m_SecSweepLoc.Set( secsweeploc );
        ws->m_SweepLoc.Set( sweeploc );
        ws->m_Dihedral = ( reldihed && i > 0 ) ? 0.0 : dihed;
        ws->m_Twist = reltwist ? twist * ( f1 - f0 ) : twist_in + ( twist - twist_in ) * f1;
        ws->m_TwistLoc = twistloc;
        ws->m_RotateMatchDiedralFlag = rotmatch;
        ws->m_TwistInXZPlaneFlag = twistxz;
        ws->ForceSpanRcTc( span * ( f1 - f0 ), rc + ( tc - rc ) * f0, rc + ( tc - rc ) * f1 );

        if ( i < m )
        {
            FileAirfoil* fa = dynamic_cast< FileAirfoil* >( ws->GetXSecCurve() );
            if ( fa )
            {
                fa->SetAirfoilName( FILLET_TAG );
                fa->SetAirfoilPnts( up[i], low[i] );
            }
        }
    }
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

    // Stations clustered inboard, where the fillet turns up the side of the fuselage.
    const int ns = std::max( settings.m_NumSect, 2 );
    vector< double > uk;
    for ( int k = 0; k < ns; k++ )
    {
        double u = u0 + ( u1 - u0 ) * ( 1.0 - cos( 0.5 * M_PI * k / ( ns - 1 ) ) );

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

    // Every airfoil the new sections are interpolated from must be a real airfoil, so the unit
    // chord frame is the one FileAirfoil uses.
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

    //==== Airfoil for each station, from the unmodified wing ====//
    const int mhalf = 60;
    vector< double > tm = AirfoilParams( mhalf );
    const double te_fade = 0.8;     // Fillet fades to nothing between here and the TE.

    vector< vector< vec3d > > up( uk.size() ), low( uk.size() );
    for ( size_t k = 0; k < uk.size(); k++ )
    {
        double u = uk[k];

        vector< double > hj( tj.size(), 0.0 );
        for ( size_t j = 0; j < tj.size(); j++ )
        {
            const Profile & p = prof[j];
            if ( p.m_Valid )
            {
                hj[j] = p.Height( dot( we.Pnt( u, tj[j] ) - p.m_C, p.m_S ) );
            }
        }

        double chord = we.Chord( u );
        vec3d le = we.Foil( u, 2.0 );
        vec3d te = we.Foil( u, 0.0 );
        double xlen = te.x() - le.x();

        vector< vec3d > q( tm.size() );
        for ( size_t i = 0; i < tm.size(); i++ )
        {
            vec3d f = we.Foil( u, tm[i] );
            double xr = ( f.x() - le.x() ) / xlen;
            double h = Interp( tj, hj, tm[i] ) * SmoothStep( ( 1.0 - xr ) / ( 1.0 - te_fade ) );
            q[i] = f + we.FoilNormal( u, tm[i] ) * ( h / chord );
            q[i].set_z( 0.0 );
        }

        // FileAirfoil wants both surfaces listed LE -> TE, sharing the LE point.
        for ( int i = mhalf; i >= 0; i-- )
        {
            low[k].push_back( q[i] );
        }
        for ( int i = mhalf; i <= 2 * mhalf; i++ )
        {
            up[k].push_back( q[i] );
        }
    }

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
        vector< vector< vec3d > > pup, plow;
        for ( int k : it->second )
        {
            fr.push_back( uk[k] - panel );
            pup.push_back( up[k] );
            plow.push_back( low[k] );
        }
        SplitPanel( wing, panel + 1, fr, pup, plow );
    }

    wing->m_SurfDirty = true;
    wing->Update();

    char buf[128];
    snprintf( buf, sizeof( buf ), "%d sections, radius %g", (int)uk.size(), R );
    msg = buf;
    return true;
}

} // namespace

namespace WingBodyFillet
{

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
