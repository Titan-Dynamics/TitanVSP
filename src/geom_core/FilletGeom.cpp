//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

// FilletGeom.cpp: Rolling ball fillet between a wing and the fuselage its root is buried in.
//
// The fillet is built separately for each of the wing's surfaces (a symmetric wing has two),
// directly in world coordinates, so the Geom's own placement is held at the identity.
//
// For one wing surface:
//  1. The junction.  Along each chordwise line of the wing, walk out from the root until the
//     line leaves the fuselage.  Around the airfoil -- lower TE, LE, upper TE -- those points
//     trace the junction loop, which wanders in and out as the fuselage's section changes.
//  2. The ball.  At each station on the loop, find the centre of the ball of radius R that
//     touches both the wing and the fuselage, in the plane across the junction.  Distances are
//     always measured to the true surfaces, so the fillet follows their real curvature.
//  3. The section.  The ball's arc between its two contact points is the fillet's face.  It is
//     closed off by a point buried in both bodies, making the fillet a closed body that unions
//     cleanly with the wing and fuselage.
//  4. The TE.  The junction has a corner at the wing's TE, where the upper and lower fillets
//     would cross.  The radius tapers to nothing approaching it, so the body ends in a point.
//  5. The surface.  The sections are skinned C2 around the loop.
//
//////////////////////////////////////////////////////////////////////

#include "FilletGeom.h"

#include "APIDefines.h"
#include "BndBox.h"
#include "IDMgr.h"
#include "Vehicle.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

using namespace vsp;

namespace
{

//==== Signed distance to a set of surfaces, negative inside ====//
class SurfDist
{
public:
    void Add( const VspSurf* surf )
    {
        if ( !surf )
        {
            return;
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
        m_U.push_back( 0.0 );
        m_W.push_back( 0.0 );
        m_D.push_back( 0.0 );
        m_P.push_back( vec3d() );
        m_Warm.push_back( false );
    }

    bool Empty() const      { return m_Surfs.empty(); }

    // Forget the last answer, so the next query does a global search.
    void Reset()
    {
        m_Warm.assign( m_Surfs.size(), false );
    }

    // Signed distance from p; q is the nearest point and n the outward normal there.
    double Eval( const vec3d & p, vec3d & q, vec3d & n )
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
                q = surf->CompPnt( u, w );
                n = surf->CompNorm( u, w ) * m_Orient[i];
                n.normalize();
                sd = dot( p - q, n ) < 0.0 ? -d : d;
            }
        }
        return sd;
    }

    double operator()( const vec3d & p )
    {
        vec3d q, n;
        return Eval( p, q, n );
    }

private:
    vector< const VspSurf* > m_Surfs;
    vector< double > m_Orient;
    vector< bool > m_Warm;
    vector< double > m_U, m_W, m_D;
    vector< vec3d > m_P;
};

double SmoothStep( double x )
{
    x = std::min( std::max( x, 0.0 ), 1.0 );
    return x * x * ( 3.0 - 2.0 * x );
}

// Parameters around the airfoil, w in [0, wmax]: lower surface TE -> LE, then upper LE -> TE.
// Cosine spaced on each side, so stations crowd the LE and TE where the junction turns.
vector< double > LoopParams( int n, double wmax )
{
    int nhalf = std::max( ( n - 1 ) / 2, 2 );
    vector< double > w( 2 * nhalf + 1 );
    for ( int i = 0; i <= nhalf; i++ )
    {
        double a = 0.5 * ( 1.0 - cos( M_PI * i / nhalf ) );
        w[i] = 0.5 * wmax * a;
        w[2 * nhalf - i] = wmax - 0.5 * wmax * a;
    }
    w[nhalf] = 0.5 * wmax;
    return w;
}

// Where the chordwise line w of the wing leaves the fuselage, walking out from the root.
bool FindJunction( const VspSurf & wing, SurfDist & fuse, double w, double & ustar )
{
    const double umax = wing.GetUMax();
    fuse.Reset();
    if ( fuse( wing.CompPnt( 0.0, w ) ) >= 0.0 )
    {
        return false;   // Root is not buried here.
    }

    const double du = umax / 400.0;
    double ulo = 0.0;
    for ( double u = du; u <= umax + 1.0e-12; u += du )
    {
        if ( fuse( wing.CompPnt( u, w ) ) >= 0.0 )
        {
            double uhi = u;
            for ( int k = 0; k < 40; k++ )
            {
                double um = 0.5 * ( ulo + uhi );
                if ( fuse( wing.CompPnt( um, w ) ) >= 0.0 )
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

// Centre of the ball of radius R touching both the wing and the fuselage near junction point j,
// in the plane through j across the junction tangent t.  qw and qf are the contact points.
bool SolveBall( SurfDist & wing, SurfDist & fuse, const vec3d & j, const vec3d & t,
                const vec3d & nw, const vec3d & nf, double R, vec3d & c, vec3d & qw, vec3d & qf )
{
    vec3d bis = nw + nf;
    if ( bis.mag() < 1.0e-6 )
    {
        return false;   // The surfaces face each other: no corner to fill.
    }

    // Exact for two planes: c - j = a ( nw + nf ) with a = R / ( 1 + nw . nf ).
    double cosg = dot( nw, nf );
    c = j + bis * ( R / std::max( 1.0 + cosg, 0.25 ) );

    vec3d e1 = bis - t * dot( bis, t );
    e1.normalize();
    vec3d e2 = cross( t, e1 );
    e2.normalize();

    wing.Reset();
    fuse.Reset();
    vec3d gw, gf;
    double dw = 0.0, df = 0.0;
    bool converged = false;
    for ( int it = 0; it < 40; it++ )
    {
        dw = wing.Eval( c, qw, gw );
        df = fuse.Eval( c, qf, gf );
        double rw = dw - R;
        double rf = df - R;
        if ( fabs( rw ) + fabs( rf ) < 1.0e-7 * R )
        {
            converged = true;
            break;
        }

        double a11 = dot( gw, e1 ), a12 = dot( gw, e2 );
        double a21 = dot( gf, e1 ), a22 = dot( gf, e2 );
        double det = a11 * a22 - a12 * a21;
        if ( fabs( det ) < 1.0e-12 )
        {
            return false;
        }
        double da = ( -rw * a22 + rf * a12 ) / det;
        double db = ( -rf * a11 + rw * a21 ) / det;

        double step = sqrt( da * da + db * db );
        if ( step > 0.5 * R )
        {
            da *= 0.5 * R / step;
            db *= 0.5 * R / step;
        }
        c = c + e1 * da + e2 * db;
    }

    if ( !converged )
    {
        converged = fabs( dw - R ) < 1.0e-4 * R && fabs( df - R ) < 1.0e-4 * R;
    }
    return converged && dist( c, j ) < 6.0 * R;
}

// The fillet for one wing surface.  Returns false if the wing's root is not in the fuselage.
bool BuildFillet( const VspSurf & wsurf, SurfDist & fuse, double R, int nstation, double taper,
                  VspSurf & out )
{
    SurfDist wing;
    wing.Add( &wsurf );

    //==== Junction loop ====//
    const double wmax = wsurf.GetWMax();
    vector< double > wst = LoopParams( nstation, wmax );
    vector< double > ws;
    vector< vec3d > jp;
    vector< double > us;
    for ( double w : wst )
    {
        double ustar;
        if ( FindJunction( wsurf, fuse, w, ustar ) )
        {
            ws.push_back( w );
            us.push_back( ustar );
            jp.push_back( wsurf.CompPnt( ustar, w ) );
        }
    }
    const int n = (int)ws.size();
    if ( n < 0.6 * wst.size() || n < 5 )
    {
        return false;
    }

    //==== One closed section per station ====//
    const int narc = 16;
    vector< double > param;
    for ( int k = 0; k <= narc; k++ )
    {
        param.push_back( 2.0 * k / narc );       // The arc runs over [0, 2] ...
    }
    param.push_back( 3.0 );                      // ... the buried point sits at 3 ...
    param.push_back( 4.0 );                      // ... and the section closes at 4.

    vector< VspCurve > ribs( n );
    vec3d mid_c, mid_p;
    double mid_r = -1.0;
    for ( int i = 0; i < n; i++ )
    {
        const vec3d & j = jp[i];

        // Radius at this station, tapering to nothing at the TE.
        double from_te = std::min( ws[i], wmax - ws[i] ) / ( 0.5 * wmax );
        double r = R * ( taper > 0.0 ? SmoothStep( from_te / taper ) : 1.0 );

        vector< vec3d > pts( narc + 2, j );
        if ( r > 1.0e-6 * R && i > 0 && i < n - 1 )
        {
            vec3d t = jp[i + 1] - jp[i - 1];
            t.normalize();

            vec3d q, nw, nf;
            wing.Reset();
            fuse.Reset();
            wing.Eval( j, q, nw );
            fuse.Eval( j, q, nf );

            // Fall back to smaller balls where a big one cannot fit, rather than leave a hole.
            vec3d c, qw, qf;
            bool ok = false;
            for ( int attempt = 0; attempt < 4 && !ok; attempt++ )
            {
                ok = SolveBall( wing, fuse, j, t, nw, nf, r, c, qw, qf );
                if ( !ok )
                {
                    r *= 0.5;
                }
            }

            if ( ok )
            {
                vec3d vw = qw - c;
                vec3d vf = qf - c;
                vw.normalize();
                vf.normalize();
                double ang = acos( std::min( std::max( dot( vw, vf ), -1.0 ), 1.0 ) );

                for ( int k = 0; k <= narc; k++ )
                {
                    double s = (double)k / narc;
                    vec3d v;
                    if ( ang < 1.0e-9 )
                    {
                        v = vw;
                    }
                    else
                    {
                        v = vw * ( sin( ( 1.0 - s ) * ang ) / sin( ang ) ) + vf * ( sin( s * ang ) / sin( ang ) );
                    }
                    pts[k] = c + v * r;
                }

                // Close the section through a point buried in both bodies.
                vec3d in = nw + nf;
                in.normalize();
                pts[narc + 1] = j - in * ( 0.3 * r );

                if ( r > mid_r )
                {
                    mid_r = r;
                    mid_c = c;
                    mid_p = pts[narc / 2];
                }
            }
        }

        ribs[i].InterpolatePCHIP( pts, param, true );
    }

    if ( mid_r <= 0.0 )
    {
        return false;
    }

    out = VspSurf();
    out.SkinC2( ribs, false );
    out.SetSurfType( vsp::NORMAL_SURF );
    out.SetMagicVParm( false );

    // The fillet's face looks out into the space the ball rolled through.
    double u, w;
    out.FindNearest( u, w, mid_p );
    if ( dot( out.CompNorm( u, w ), mid_c - mid_p ) < 0.0 )
    {
        out.FlipNormal();
    }
    return true;
}

} // namespace

//==== Constructor ====//
FilletGeom::FilletGeom( Vehicle* vehicle_ptr ) : Geom( vehicle_ptr )
{
    m_Name = "FilletGeom";
    m_Type.m_Name = "Fillet";
    m_Type.m_Type = FILLET_GEOM_TYPE;

    m_Radius.Init( "Radius", "Design", this, 0.0, 0.0, 1.0e12 );
    m_Radius.SetDescript( "Radius of the ball rolled along the junction; 0 picks 8% of the chord there" );

    m_NumStations.Init( "NumStations", "Design", this, 41, 9, 401 );
    m_NumStations.SetDescript( "Number of cross-sections around the junction" );

    m_TETaper.Init( "TETaper", "Design", this, 0.15, 0.0, 1.0 );
    m_TETaper.SetDescript( "Fraction of the junction, from the TE, over which the radius tapers to nothing" );

    m_TessU = 3;
    m_TessW = 25;

    m_MainSurfVec.clear();
}

//==== Destructor ====//
FilletGeom::~FilletGeom()
{
}

xmlNodePtr FilletGeom::EncodeXml( xmlNodePtr & node )
{
    xmlNodePtr geom_node = Geom::EncodeXml( node );

    xmlNodePtr child_node = xmlNewChild( geom_node, nullptr, BAD_CAST "Fillet", nullptr );
    if ( child_node )
    {
        XmlUtil::AddStringNode( child_node, "FuseID", m_FuseID );
    }
    return child_node;
}

xmlNodePtr FilletGeom::DecodeXml( xmlNodePtr & node )
{
    xmlNodePtr geom_node = Geom::DecodeXml( node );

    xmlNodePtr child_node = XmlUtil::GetNode( geom_node, "Fillet", 0 );
    if ( child_node )
    {
        SetFuse( IDMgr.RemapRefID( XmlUtil::FindString( child_node, "FuseID", m_FuseID ) ) );
    }
    return child_node;
}

// Built from the wing it hangs off and the fuselage it blends into.
void FilletGeom::GetDependencyIDs( vector< string > &ids )
{
    Geom::GetDependencyIDs( ids );
    if ( !m_FuseID.empty() )
    {
        ids.push_back( m_FuseID );
    }
}

void FilletGeom::ApplyScale( double currentScale )
{
    m_Radius *= currentScale;
}

// The fillet is built in world coordinates, so its own placement is held at the identity.
void FilletGeom::UpdateCopyXFormParms()
{
    m_TransAttachFlag = vsp::ATTACH_TRANS_NONE;
    m_RotAttachFlag = vsp::ATTACH_ROT_NONE;
    m_AbsRelFlag = vsp::REL;

    m_XRelLoc = 0.0;
    m_YRelLoc = 0.0;
    m_ZRelLoc = 0.0;
    m_XRelRot = 0.0;
    m_YRelRot = 0.0;
    m_ZRelRot = 0.0;

    // Each of the wing's surfaces, mirrored copies included, gets a fillet of its own.
    m_SymPlanFlag = 0;
    m_SymAxFlag = 0;

    Parm* fixed[] = { &m_TransAttachFlag, &m_RotAttachFlag, &m_AbsRelFlag,
                      &m_XRelLoc, &m_YRelLoc, &m_ZRelLoc, &m_XRelRot, &m_YRelRot, &m_ZRelRot,
                      &m_XLoc, &m_YLoc, &m_ZLoc, &m_XRot, &m_YRot, &m_ZRot,
                      &m_SymPlanFlag, &m_SymAxFlag, &m_SymAncestor, &m_SymAncestOriginFlag, &m_SymRotN,
                      &m_Origin };
    for ( Parm* p : fixed )
    {
        p->Deactivate();
    }
}

// Blend into a different fuselage, rebuilding whenever that one changes.
void FilletGeom::SetFuse( const string & id )
{
    if ( id == m_FuseID )
    {
        return;
    }

    Geom* old_fuse = m_Vehicle->FindGeom( m_FuseID );
    if ( old_fuse )
    {
        old_fuse->RemoveStepChildID( m_ID );
    }

    m_FuseID = id;

    Geom* new_fuse = m_Vehicle->FindGeom( m_FuseID );
    if ( new_fuse )
    {
        new_fuse->AddStepChildID( m_ID );
    }
    m_SurfDirty = true;
}

// The fuselage the wing's root is buried in: the one holding most of its root section.
Geom* FilletGeom::FindFuse( Geom* wing )
{
    Geom* current = m_Vehicle->FindGeom( m_FuseID );
    if ( current && current->GetType().m_Type == FUSELAGE_GEOM_TYPE && !current->DependsOn( m_ID ) )
    {
        return current;
    }

    const VspSurf* root = wing->GetSurfPtr( 0 );
    if ( !root )
    {
        return nullptr;
    }

    Geom* best = nullptr;
    int best_in = 0;
    for ( Geom* g : m_Vehicle->FindGeomVec( m_Vehicle->GetGeomVec() ) )
    {
        if ( !g || g->GetType().m_Type != FUSELAGE_GEOM_TYPE || g->DependsOn( m_ID ) )
        {
            continue;
        }

        SurfDist fd;
        for ( int i = 0; i < g->GetNumTotalSurfs(); i++ )
        {
            fd.Add( g->GetSurfPtr( i ) );
        }
        if ( fd.Empty() )
        {
            continue;
        }

        int nin = 0;
        for ( double w : LoopParams( 17, root->GetWMax() ) )
        {
            fd.Reset();
            if ( fd( root->CompPnt( 0.0, w ) ) < 0.0 )
            {
                nin++;
            }
        }
        if ( nin > best_in )
        {
            best_in = nin;
            best = g;
        }
    }
    return best;
}

void FilletGeom::UpdateSurf()
{
    m_MainSurfVec.clear();

    Geom* wing = m_Vehicle->FindGeom( m_ParentID );
    if ( !wing || wing->GetType().m_Type != MS_WING_GEOM_TYPE )
    {
        m_Status = "A fillet must be added to a wing.";
        return;
    }

    Geom* fuse = FindFuse( wing );
    if ( !fuse )
    {
        SetFuse( string() );
        m_Status = "The wing's root is not inside a fuselage.";
        return;
    }
    SetFuse( fuse->GetID() );

    SurfDist fd;
    for ( int i = 0; i < fuse->GetNumTotalSurfs(); i++ )
    {
        fd.Add( fuse->GetSurfPtr( i ) );
    }

    // First build: size the ball from the chord where the wing comes out of the fuselage.
    if ( m_Radius() <= 0.0 )
    {
        const VspSurf* ws = wing->GetSurfPtr( 0 );
        double ute, ule;
        if ( ws && FindJunction( *ws, fd, 0.0, ute ) && FindJunction( *ws, fd, 0.5 * ws->GetWMax(), ule ) )
        {
            double chord = dist( ws->CompPnt( ute, 0.0 ), ws->CompPnt( ule, 0.5 * ws->GetWMax() ) );
            m_Radius.Set( 0.08 * chord );
        }
        if ( m_Radius() <= 0.0 )
        {
            m_Status = "Could not size the fillet; set its radius.";
            return;
        }
    }

    int nbuilt = 0;
    for ( int i = 0; i < wing->GetNumTotalSurfs(); i++ )
    {
        const VspSurf* ws = wing->GetSurfPtr( i );
        if ( !ws )
        {
            continue;
        }
        VspSurf surf;
        if ( BuildFillet( *ws, fd, m_Radius(), m_NumStations(), m_TETaper(), surf ) )
        {
            m_MainSurfVec.push_back( surf );
            nbuilt++;
        }
    }

    if ( nbuilt == 0 )
    {
        m_Status = "The wing's root is not buried in " + fuse->GetName() + ".";
    }
    else
    {
        m_Status = "Blending " + wing->GetName() + " into " + fuse->GetName() + ".";
    }
}
