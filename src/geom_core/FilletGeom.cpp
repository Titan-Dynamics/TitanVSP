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
//  2. The balls.  A ball of radius R touching both bodies has its centre where the wing and the
//     fuselage, each pushed out along its normal by R, meet.  One ball is found from the junction;
//     from there Newton follows that intersection around the loop on the two surfaces' own
//     parameters, so the contacts move smoothly even round the LE, where a thin wing's upper and
//     lower surfaces are both close by.  Stations are added wherever the section turns quickly.
//  3. The sections.  The ball's arc between its two contacts is the fillet's face.  The face runs
//     on past each contact by Sink, along the surface and easing under it, so the fillet fades
//     into the wing and fuselage.  The section closes through a point past the junction, buried
//     in both, so the fillet is a closed body overlapping both.
//  4. The TE.  The junction has a corner at the wing's TE, where the upper and lower fillets
//     would cross.  The radius tapers to nothing approaching it, so the body ends in a point.
//  5. The surface.  The sections are skinned C2 around the loop, spaced as they lie along it.
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

// Unit normal of surf at ( u, w ), times orient.  Where the surface's own normal vanishes -- an
// airfoil's LE and TE, the nose of a fuselage -- it is the average of the normals either side.
// Some airfoils carry a tiny patch of zero length at the LE, over which the normal vanishes
// throughout, so the search widens until it reaches surface that has one.
bool Finite( const vec3d & v )
{
    return std::isfinite( v.x() ) && std::isfinite( v.y() ) && std::isfinite( v.z() );
}

vec3d SafeNormal( const VspSurf & surf, double u, double w, double orient )
{
    vec3d n = surf.CompNorm( u, w );
    if ( !Finite( n ) )
    {
        n = vec3d();
    }
    for ( double e = 1.0e-5; n.mag() < 1.0e-12 && e < 0.05; e *= 4.0 )
    {
        double w0 = std::max( w - e * surf.GetWMax(), 0.0 ), w1 = std::min( w + e * surf.GetWMax(), surf.GetWMax() );
        double u0 = std::max( u - e * surf.GetUMax(), 0.0 ), u1 = std::min( u + e * surf.GetUMax(), surf.GetUMax() );
        vec3d na = surf.CompNorm( u, w0 ), nb = surf.CompNorm( u, w1 );
        na = Finite( na ) ? na : vec3d();
        nb = Finite( nb ) ? nb : vec3d();
        na.normalize();
        nb.normalize();
        n = na + nb;
        if ( n.mag() < 1.0e-12 )
        {
            na = surf.CompNorm( u0, w );
            nb = surf.CompNorm( u1, w );
            na = Finite( na ) ? na : vec3d();
            nb = Finite( nb ) ? nb : vec3d();
            na.normalize();
            nb.normalize();
            n = na + nb;
        }
    }
    n.normalize();
    return n * orient;
}

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

        // A grid of samples over the surface, to start each global search near the answer.
        // VspSurf's own global search is far too slow to call as often as this does.
        Grid grid;
        const int nu = std::max( 60, 8 * (int)std::ceil( surf->GetUMax() ) );
        const int nw = 64;
        for ( int i = 0; i <= nu; i++ )
        {
            double u = surf->GetUMax() * i / nu;
            for ( int j = 0; j <= nw; j++ )
            {
                double w = surf->GetWMax() * j / nw;
                grid.m_U.push_back( u );
                grid.m_W.push_back( w );
                grid.m_P.push_back( surf->CompPnt( u, w ) );
            }
        }
        m_Grids.push_back( grid );
        m_Surfs.push_back( surf );
        m_Orient.push_back( 1.0 );

        // Fix the sense of the normals by looking down on the surface from well above it.
        BndBox bb;
        surf->GetBoundingBox( bb );
        vec3d above = bb.GetCenter() + vec3d( 0.0, 0.0, 2.0 * bb.DiagDist() + 1.0 );
        double u, w;
        Nearest( m_Surfs.size() - 1, above, u, w );
        double side = dot( above - surf->CompPnt( u, w ), SafeNormal( *surf, u, w, 1.0 ) );
        m_Orient.back() = side < 0.0 ? -1.0 : 1.0;
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
                u = m_U[i];
                w = m_W[i];
                d = surf->FindNearest( u, w, p, m_U[i], m_W[i] );
                // Distance to a surface is 1-Lipschitz.  A local solve landing farther away than
                // this bound has found the wrong minimum, so search globally.
                if ( !std::isfinite( d ) || !std::isfinite( u ) || !std::isfinite( w ) ||
                     d > m_D[i] + dist( p, m_P[i] ) + 1.0e-9 )
                {
                    d = Nearest( i, p, u, w );
                }
            }
            else
            {
                d = Nearest( i, p, u, w );
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
                n = SafeNormal( *surf, u, w, m_Orient[i] );
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

    size_t NumSurfs() const                     { return m_Surfs.size(); }
    const VspSurf* Surf( size_t i ) const       { return m_Surfs[i]; }
    double Orient( size_t i ) const             { return m_Orient[i]; }

    // Outward unit normal of surface i at ( u, w ).
    vec3d Normal( size_t i, double u, double w ) const
    {
        return SafeNormal( *m_Surfs[i], u, w, m_Orient[i] );
    }

    // Global nearest point to p over all surfaces: which surface, and where on it.
    double GlobalNearest( const vec3d & p, size_t & isurf, double & u, double & w ) const
    {
        double best = DBL_MAX;
        for ( size_t i = 0; i < m_Surfs.size(); i++ )
        {
            double ui, wi;
            double d = Nearest( i, p, ui, wi );
            if ( d < best )
            {
                best = d;
                isurf = i;
                u = ui;
                w = wi;
            }
        }
        return best;
    }

private:
    struct Grid
    {
        vector< double > m_U, m_W;
        vector< vec3d > m_P;
    };

    // Global nearest point on surface i: the closest grid sample, polished by a local solve.
    // The few samples nearest p are each polished, so a sample on the wrong lobe of a thin
    // surface (the other side of a wing) cannot win by being a hair closer.
    double Nearest( size_t i, const vec3d & p, double & u, double & w ) const
    {
        const Grid & g = m_Grids[i];
        const int nkeep = 4;
        int best[nkeep];
        double bestd[nkeep];
        for ( int k = 0; k < nkeep; k++ )
        {
            best[k] = -1;
            bestd[k] = DBL_MAX;
        }
        for ( size_t k = 0; k < g.m_P.size(); k++ )
        {
            double d2 = dist_squared( p, g.m_P[k] );
            if ( d2 < bestd[nkeep - 1] )
            {
                int s = nkeep - 1;
                while ( s > 0 && d2 < bestd[s - 1] )
                {
                    bestd[s] = bestd[s - 1];
                    best[s] = best[s - 1];
                    s--;
                }
                bestd[s] = d2;
                best[s] = (int)k;
            }
        }

        // Start from the best sample itself, so u and w are always something sensible.
        double dmin = sqrt( bestd[0] );
        u = g.m_U[best[0]];
        w = g.m_W[best[0]];
        for ( int k = 0; k < nkeep && best[k] >= 0; k++ )
        {
            double uk = g.m_U[best[k]], wk = g.m_W[best[k]];
            double d = m_Surfs[i]->FindNearest( uk, wk, p, g.m_U[best[k]], g.m_W[best[k]] );
            if ( std::isfinite( d ) && std::isfinite( uk ) && std::isfinite( wk ) && d < dmin )
            {
                dmin = d;
                u = uk;
                w = wk;
            }
        }
        return dmin;
    }

    vector< Grid > m_Grids;
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

    const int nstep = 400;
    double ulo = 0.0;
    for ( int k = 1; k <= nstep; k++ )
    {
        double u = umax * k / nstep;    // Never past the end of the surface.
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

//==== One station of the fillet: the ball, and where it touches ====//
struct Station
{
    double m_W = 0.0;       // Chordwise parameter of the wing contact, and of the junction.
    double m_R = 0.0;       // Ball radius here.
    double m_UW = 0.0;      // The wing contact is at ( m_UW, m_W ).
    double m_UF = 0.0;      // The fuselage contact is at ( m_UF, m_WF ).
    double m_WF = 0.0;
    bool m_Degenerate = false;  // No ball: the section is the junction point itself.
    vec3d m_C, m_QW, m_QF, m_J;
    vec3d m_NW, m_NF;       // Outward normals of the wing and fuselage at the contacts.
};

//==== The wing and fuselage pushed out along their normals by the ball radius ====//
// A ball of radius r touching both has its centre where the two offset surfaces meet, so the
// fillet's spine is their intersection.  Solving on the surfaces' own parameters keeps each
// contact on its own patch of surface, so it moves smoothly from one station to the next --
// unlike nearest point queries, which jump between the upper and lower surfaces of a thin wing.
class OffsetPair
{
public:
    OffsetPair( const VspSurf & wing, double wing_orient, const VspSurf & fuse, double fuse_orient ) :
        m_Wing( wing ), m_Fuse( fuse ), m_WingOrient( wing_orient ), m_FuseOrient( fuse_orient )
    {
        BndBox bb;
        fuse.GetBoundingBox( bb );
        m_Tol = 1.0e-9 * bb.DiagDist();
        m_FuseClosedW = fuse.IsClosedW();
    }

    // Surfaces must only be evaluated inside their parameter ranges.  A closed fuselage wraps
    // round in w; everything else is held at its ends.
    double WingU( double u ) const  { return std::min( std::max( u, 0.0 ), m_Wing.GetUMax() ); }
    double WingW( double w ) const  { return std::min( std::max( w, 0.0 ), m_Wing.GetWMax() ); }
    double FuseU( double u ) const  { return std::min( std::max( u, 0.0 ), m_Fuse.GetUMax() ); }
    double FuseW( double w ) const
    {
        const double wmax = m_Fuse.GetWMax();
        if ( m_FuseClosedW )
        {
            w = fmod( w, wmax );
            return w < 0.0 ? w + wmax : w;
        }
        return std::min( std::max( w, 0.0 ), wmax );
    }

    vec3d WingNormal( double u, double w ) const    { return SafeNormal( m_Wing, WingU( u ), WingW( w ), m_WingOrient ); }
    vec3d FuseNormal( double u, double w ) const    { return SafeNormal( m_Fuse, FuseU( u ), FuseW( w ), m_FuseOrient ); }
    vec3d WingPnt( double u, double w ) const       { return m_Wing.CompPnt( WingU( u ), WingW( w ) ); }
    vec3d FusePnt( double u, double w ) const       { return m_Fuse.CompPnt( FuseU( u ), FuseW( w ) ); }

    // The point on the wing (fuselage) nearest p, searching from ( u, w ), which are updated to
    // where it is.  n is the outward normal there.
    vec3d OnWing( const vec3d & p, double & u, double & w, vec3d & n ) const
    {
        double u0 = WingU( u ), w0 = WingW( w );
        m_Wing.FindNearest( u, w, p, u0, w0 );
        if ( !std::isfinite( u ) || !std::isfinite( w ) )
        {
            u = u0;
            w = w0;
        }
        u = WingU( u );
        w = WingW( w );
        n = WingNormal( u, w );
        return WingPnt( u, w );
    }

    vec3d OnFuse( const vec3d & p, double & u, double & w, vec3d & n ) const
    {
        double u0 = FuseU( u ), w0 = FuseW( w );
        m_Fuse.FindNearest( u, w, p, u0, w0 );
        if ( !std::isfinite( u ) || !std::isfinite( w ) )
        {
            u = u0;
            w = w0;
        }
        u = FuseU( u );
        w = FuseW( w );
        n = FuseNormal( u, w );
        return FusePnt( u, w );
    }

    vec3d Wing( double u, double w, double r ) const
    {
        return WingPnt( u, w ) + WingNormal( u, w ) * r;
    }

    vec3d Fuse( double u, double w, double r ) const
    {
        return FusePnt( u, w ) + FuseNormal( u, w ) * r;
    }

    // Newton for the ball of radius r touching the wing on its chordwise line w: solves
    // Wing( uw, w ) = Fuse( uf, wf ) for uw, uf and wf, starting from the values passed in.
    bool Solve( double w, double r, double & uw, double & uf, double & wf ) const
    {
        const double h = 1.0e-6;
        const double umaxw = m_Wing.GetUMax();
        const double umaxf = m_Fuse.GetUMax();
        const double wmaxf = m_Fuse.GetWMax();

        if ( !std::isfinite( uw ) || !std::isfinite( uf ) || !std::isfinite( wf ) || !std::isfinite( w ) || !std::isfinite( r ) )
        {
            return false;
        }

        for ( int it = 0; it < 40; it++ )
        {
            vec3d g = Wing( uw, w, r ) - Fuse( uf, wf, r );
            if ( !Finite( g ) )
            {
                return false;
            }
            if ( g.mag() < m_Tol )
            {
                return true;
            }

            // Central differences, one sided where a parameter is at the end of its range.
            double ua0 = WingU( uw - h ), ua1 = WingU( uw + h );
            double ub0 = FuseU( uf - h ), ub1 = FuseU( uf + h );
            double wc0 = m_FuseClosedW ? wf - h : FuseW( wf - h );
            double wc1 = m_FuseClosedW ? wf + h : FuseW( wf + h );
            vec3d a = ( Wing( ua1, w, r ) - Wing( ua0, w, r ) ) / ( ua1 - ua0 );
            vec3d b = ( Fuse( ub1, wf, r ) - Fuse( ub0, wf, r ) ) / ( ub0 - ub1 );
            vec3d c = ( Fuse( uf, wc1, r ) - Fuse( uf, wc0, r ) ) / ( wc0 - wc1 );

            double det = dot( a, cross( b, c ) );
            if ( !std::isfinite( det ) || fabs( det ) < 1.0e-300 )
            {
                return false;
            }
            vec3d rhs = g * -1.0;
            double du = dot( rhs, cross( b, c ) ) / det;
            double dsf = dot( a, cross( rhs, c ) ) / det;
            double dwf = dot( a, cross( b, rhs ) ) / det;

            // Keep each step small next to the surfaces' parameter ranges.
            double s = 1.0;
            s = std::min( s, 0.05 * umaxw / std::max( fabs( du ), 1.0e-300 ) );
            s = std::min( s, 0.05 * umaxf / std::max( fabs( dsf ), 1.0e-300 ) );
            s = std::min( s, 0.05 * wmaxf / std::max( fabs( dwf ), 1.0e-300 ) );

            uw = WingU( uw + s * du );
            uf = FuseU( uf + s * dsf );
            wf = FuseW( wf + s * dwf );
        }
        return ( Wing( uw, w, r ) - Fuse( uf, wf, r ) ).mag() < 1.0e3 * m_Tol;
    }

    const VspSurf & m_Wing;
    const VspSurf & m_Fuse;

private:
    double m_WingOrient, m_FuseOrient;
    double m_Tol;
    bool m_FuseClosedW;
};

// Largest change in direction, from the ball's centre, of either contact between two stations.
double TurnAngle( const Station & a, const Station & b )
{
    if ( a.m_Degenerate || b.m_Degenerate )
    {
        return 0.0;
    }
    vec3d aw = a.m_QW - a.m_C, bw = b.m_QW - b.m_C;
    vec3d af = a.m_QF - a.m_C, bf = b.m_QF - b.m_C;
    aw.normalize();
    bw.normalize();
    af.normalize();
    bf.normalize();
    double cw = std::min( std::max( dot( aw, bw ), -1.0 ), 1.0 );
    double cf = std::min( std::max( dot( af, bf ), -1.0 ), 1.0 );
    return std::max( acos( cw ), acos( cf ) );
}

// The fillet for one wing surface.  Returns false if the wing's root is not in the fuselage.
bool BuildFillet( const VspSurf & wsurf, SurfDist & fuse, double R, int nstation, double taper,
                  double sink, VspSurf & out )
{
    SurfDist wing;
    wing.Add( &wsurf );
    const double wmax = wsurf.GetWMax();

    // The fillet comes to a point just short of the TE at each end (see below), so the radius
    // tapers to nothing there.
    const double wtip = 0.002 * wmax;
    auto radius_at = [&]( double w )
    {
        double from_tip = ( std::min( w, wmax - w ) - wtip ) / ( 0.5 * wmax - wtip );
        if ( from_tip <= 0.0 )
        {
            return 0.0;
        }
        return R * ( taper > 0.0 ? SmoothStep( from_tip / taper ) : 1.0 );
    };

    auto junction = [&]( double w, vec3d & j )
    {
        double ustar;
        if ( !FindJunction( wsurf, fuse, w, ustar ) )
        {
            return false;
        }
        j = wsurf.CompPnt( ustar, w );
        return true;
    };

    //==== Seed: one ball found from the junction, then pinned to the surfaces ====//
    Station seed;
    size_t ifuse = 0;
    bool seeded = false;
    for ( double f : { 0.25, 0.75, 0.15, 0.85, 0.35, 0.65, 0.45, 0.55 } )
    {
        double w = f * wmax;
        double dw = 0.002 * wmax;
        vec3d j, j0, j1;
        if ( !junction( w, j ) || !junction( w - dw, j0 ) || !junction( w + dw, j1 ) )
        {
            continue;
        }
        vec3d t = j1 - j0;
        t.normalize();

        vec3d q, nw, nf;
        wing.Reset();
        fuse.Reset();
        wing.Eval( j, q, nw );
        fuse.Eval( j, q, nf );

        vec3d c, qw, qf;
        if ( !SolveBall( wing, fuse, j, t, nw, nf, radius_at( w ), c, qw, qf ) )
        {
            continue;
        }

        size_t iw;
        double uw, ww, uf, wf;
        wing.GlobalNearest( qw, iw, uw, ww );
        fuse.GlobalNearest( qf, ifuse, uf, wf );

        OffsetPair op( wsurf, wing.Orient( 0 ), *fuse.Surf( ifuse ), fuse.Orient( ifuse ) );
        if ( op.Solve( ww, radius_at( ww ), uw, uf, wf ) )
        {
            seed.m_W = ww;
            seed.m_R = radius_at( ww );
            seed.m_UW = uw;
            seed.m_UF = uf;
            seed.m_WF = wf;
            seeded = true;
            break;
        }
    }
    if ( !seeded )
    {
        return false;
    }

    OffsetPair op( wsurf, wing.Orient( 0 ), *fuse.Surf( ifuse ), fuse.Orient( ifuse ) );

    // Fill in a solved station, and check its ball clears both bodies everywhere, not only
    // where it touches them.
    auto finish = [&]( Station & s )
    {
        s.m_C = op.Wing( s.m_UW, s.m_W, s.m_R );
        s.m_QW = wsurf.CompPnt( s.m_UW, s.m_W );
        s.m_QF = op.m_Fuse.CompPnt( s.m_UF, s.m_WF );
        s.m_NW = op.WingNormal( s.m_UW, s.m_W );
        s.m_NF = op.FuseNormal( s.m_UF, s.m_WF );
        if ( !Finite( s.m_C ) || !Finite( s.m_QW ) || !Finite( s.m_QF ) || !Finite( s.m_NW ) || !Finite( s.m_NF ) )
        {
            return false;
        }
        wing.Reset();
        fuse.Reset();
        if ( wing( s.m_C ) < 0.98 * s.m_R || fuse( s.m_C ) < 0.98 * s.m_R )
        {
            return false;
        }
        return junction( s.m_W, s.m_J );
    };

    // Solve at w starting from 'from', walking there in smaller steps if a big one fails.
    auto solve_at = [&]( const Station & from, double w, Station & s )
    {
        Station cur = from;
        double wcur = from.m_W;
        double step = w - wcur;
        while ( fabs( w - wcur ) > 1.0e-12 )
        {
            if ( fabs( step ) < 1.0e-6 * wmax )
            {
                return false;
            }
            double wt = fabs( step ) >= fabs( w - wcur ) ? w : wcur + step;
            double uw = cur.m_UW, uf = cur.m_UF, wf = cur.m_WF;
            if ( op.Solve( wt, radius_at( wt ), uw, uf, wf ) )
            {
                cur.m_W = wt;
                cur.m_R = radius_at( wt );
                cur.m_UW = uw;
                cur.m_UF = uf;
                cur.m_WF = wf;
                wcur = wt;
            }
            else
            {
                step *= 0.5;
            }
        }
        s = cur;
        return true;
    };

    // Stations where the ball has shrunk to nothing are just the junction point, so the fillet
    // comes to a point at each end of the TE.
    const double rmin = 1.0e-3 * R;
    auto degenerate_at = [&]( double w, Station & s )
    {
        s = Station();
        s.m_W = w;
        s.m_Degenerate = true;
        if ( !junction( w, s.m_J ) )
        {
            return false;
        }
        s.m_C = s.m_QW = s.m_QF = s.m_J;
        return true;
    };

    //==== March around the junction from the seed, both ways ====//
    vector< Station > stations;
    if ( !finish( seed ) )
    {
        return false;
    }
    stations.push_back( seed );

    // The two ends come to their points a hair short of the TE, one on the lower surface and one
    // on the upper.  Ending exactly on the TE would put both points on the same spot, pinching the
    // fillet into a shape mesh booleans reject.
    vector< double > targets = { wtip };
    for ( double w : LoopParams( nstation, wmax ) )
    {
        if ( w > wtip && w < wmax - wtip )
        {
            targets.push_back( w );
        }
    }
    targets.push_back( wmax - wtip );

    for ( int dir : { 1, -1 } )
    {
        Station last = seed;
        vector< double > ws;
        for ( double w : targets )
        {
            if ( ( dir > 0 && w > seed.m_W + 1.0e-9 ) || ( dir < 0 && w < seed.m_W - 1.0e-9 ) )
            {
                ws.push_back( w );
            }
        }
        if ( dir < 0 )
        {
            std::reverse( ws.begin(), ws.end() );
        }

        for ( double w : ws )
        {
            Station s;
            if ( radius_at( w ) < rmin )
            {
                // Too small to matter.  Only the two ends, added below, close to a point; a second
                // point just inside an end would pinch the fillet there.
                continue;
            }
            if ( !solve_at( last, w, s ) )
            {
                break;  // Lost the ball; the fillet ends at the last good station.
            }
            last = s;
            if ( finish( s ) )
            {
                stations.push_back( s );
            }
        }
    }

    std::sort( stations.begin(), stations.end(), []( const Station & a, const Station & b ) { return a.m_W < b.m_W; } );

    //==== Refine where the section turns quickly, as it does rounding the LE ====//
    const double max_turn = 5.0 * M_PI / 180.0;
    for ( int pass = 0; pass < 6 && (int)stations.size() < 8 * nstation; pass++ )
    {
        vector< Station > refined;
        bool added = false;
        for ( size_t i = 0; i < stations.size(); i++ )
        {
            refined.push_back( stations[i] );
            if ( i + 1 == stations.size() )
            {
                break;
            }
            const Station & a = stations[i];
            const Station & b = stations[i + 1];
            if ( TurnAngle( a, b ) > max_turn && b.m_W - a.m_W > 1.0e-4 * wmax )
            {
                Station m;
                if ( solve_at( a, 0.5 * ( a.m_W + b.m_W ), m ) && finish( m ) )
                {
                    refined.push_back( m );
                    added = true;
                }
            }
        }
        stations.swap( refined );
        if ( !added )
        {
            break;
        }
    }

    // Close off both ends at the TE in a point.
    for ( double w : { wtip, wmax - wtip } )
    {
        bool have = false;
        for ( const Station & s : stations )
        {
            have = have || fabs( s.m_W - w ) < 1.0e-9;
        }
        Station s;
        if ( !have && degenerate_at( w, s ) )
        {
            stations.push_back( s );
        }
    }
    std::sort( stations.begin(), stations.end(), []( const Station & a, const Station & b ) { return a.m_W < b.m_W; } );

    const int n = (int)stations.size();
    if ( n < 5 )
    {
        return false;
    }

    //==== One closed section per station ====//
    // The fillet's face -- carried on past the wing contact along the wing, round the arc, and on
    // past the fuselage contact along the fuselage -- then closed through a point past the
    // junction, buried in both bodies.
    const int narc = 16;
    const int next = 3;                     // Points on each extension of the face.
    const int nface = 2 * next + narc + 1;
    vector< double > param;
    for ( int j = next; j >= 1; j-- )
    {
        param.push_back( 0.5 * ( next - j ) / next );   // Along the wing, over [0, 0.5) ...
    }
    for ( int k = 0; k <= narc; k++ )
    {
        param.push_back( 0.5 + 2.0 * k / narc );        // ... the arc, over [0.5, 2.5] ...
    }
    for ( int j = 1; j <= next; j++ )
    {
        param.push_back( 2.5 + 0.5 * j / next );        // ... along the fuselage, to 3.
    }
    param.push_back( 3.5 );                             // Buried point.
    param.push_back( 4.0 );                             // Closes back on the start.

    // Sink widens the fillet: its face runs on past each contact along the surface, easing under
    // it, so it fades into the wing and fuselage instead of ending on a line.  The face dips to
    // this fraction of the run below the surface by its end.  The run shrinks with the ball toward
    // the TE, and is kept within reason of the ball's size.
    const double dip = 0.15;
    const double sink0 = std::min( std::max( sink, 0.0 ), 4.0 * R );

    vector< VspCurve > ribs( n );
    vector< vec3d > centroid( n );
    vec3d mid_c, mid_p;
    double mid_r = -1.0;
    for ( int i = 0; i < n; i++ )
    {
        const Station & s = stations[i];
        vector< vec3d > pts( nface + 1, s.m_J );
        if ( !s.m_Degenerate )
        {
            vec3d vw = s.m_QW - s.m_C;
            vec3d vf = s.m_QF - s.m_C;
            vw.normalize();
            vf.normalize();
            double ang = acos( std::min( std::max( dot( vw, vf ), -1.0 ), 1.0 ) );
            for ( int k = 0; k <= narc; k++ )
            {
                double t = (double)k / narc;
                vec3d v = vw;
                if ( ang > 1.0e-9 )
                {
                    v = vw * ( sin( ( 1.0 - t ) * ang ) / sin( ang ) ) + vf * ( sin( t * ang ) / sin( ang ) );
                }
                pts[next + k] = s.m_C + v * s.m_R;
            }

            // The directions the arc leaves its two ends, carried on along each surface.
            double sk = sink0 * s.m_R / R;
            vec3d pn = cross( vw, vf );
            pn.normalize();
            vec3d tw = cross( vw, pn );     // At the wing contact, away from the arc.
            vec3d tf = cross( pn, vf );     // At the fuselage contact, away from the arc.

            double uw = s.m_UW, ww = s.m_W, uf = s.m_UF, wf = s.m_WF;
            for ( int j = 1; j <= next; j++ )
            {
                double a = (double)j / next;
                vec3d n;
                vec3d p = op.OnWing( s.m_QW + tw * ( sk * a ), uw, ww, n );
                pts[next - j] = ( ang > 1.0e-9 ) ? p - n * ( dip * sk * a * a ) : s.m_QW;
                p = op.OnFuse( s.m_QF + tf * ( sk * a ), uf, wf, n );
                pts[next + narc + j] = ( ang > 1.0e-9 ) ? p - n * ( dip * sk * a * a ) : s.m_QF;
            }

            // Close the section through a point past the junction, buried in both bodies.
            vec3d in = s.m_J - s.m_C;
            in.normalize();
            pts[nface] = s.m_J + in * ( 0.3 * s.m_R + dip * sk );

            if ( s.m_R > mid_r )
            {
                mid_r = s.m_R;
                mid_c = s.m_C;
                mid_p = pts[next + narc / 2];
            }
        }

        // The face as one smooth spline, and the buried legs as straight lines.
        VspCurve face, legs;
        vector< vec3d > face_pts( pts.begin(), pts.begin() + nface );
        vector< double > face_param( param.begin(), param.begin() + nface );
        face.InterpolateCSpline( face_pts, face_param, false );
        legs.InterpolateLinear( { pts[nface - 1], pts[nface], pts[0] },
                                { param[nface - 1], param[nface], param[nface + 1] }, false );
        piecewise_curve_type pc = face.GetCurve();
        pc.push_back( legs.GetCurve() );
        // Skinning wants every segment of every section the same degree.
        pc.degree_promote_to( 3 );
        ribs[i].SetCurve( pc );

        vec3d sum;
        for ( const vec3d & p : pts )
        {
            sum = sum + p;
        }
        centroid[i] = sum / (double)pts.size();
    }

    if ( mid_r <= 0.0 )
    {
        return false;
    }

    // Skin with the sections spaced as they are along the junction, so refined stretches do not
    // make the surface overshoot.
    vector< double > uparam( n, 0.0 );
    for ( int i = 1; i < n; i++ )
    {
        uparam[i] = uparam[i - 1] + std::max( dist( centroid[i], centroid[i - 1] ), 1.0e-9 * R );
    }
    double uscale = ( n - 1 ) / uparam.back();
    for ( double & u : uparam )
    {
        u *= uscale;
    }

    out = VspSurf();
    out.SkinCX( ribs, rib_data_type::C2, uparam, false );

    // Tessellation expects each section of a surface to span exactly one unit of u; with the
    // spacing above, the short first section was given no rows at all, leaving the point of the
    // fillet out of the mesh and a pinhole in it.  Relabel every section to span one unit.  Each
    // patch keeps its exact shape -- only its parameter interval changes.
    piecewise_surface_type* ps = out.GetBezierSurface();
    const int nup = ps->number_u_patches();
    const int nvp = ps->number_v_patches();
    vector< double > dus( nup, 1.0 ), dvs( nvp );
    for ( int vi = 0; vi < nvp; vi++ )
    {
        dvs[vi] = ps->get_dv( vi );
    }
    piecewise_surface_type relabeled;
    relabeled.init_uv( dus.begin(), dus.end(), dvs.begin(), dvs.end(), 0.0, ps->get_v0() );
    for ( int ui = 0; ui < nup; ui++ )
    {
        for ( int vi = 0; vi < nvp; vi++ )
        {
            piecewise_surface_type::surface_type patch;
            ps->get( patch, ui, vi );
            relabeled.set( patch, ui, vi );
        }
    }
    *ps = relabeled;
    out.SetSurfType( vsp::NORMAL_SURF );
    out.SetMagicVParm( false );

    // The fillet's face looks out into the space the ball rolled through.
    double u, w;
    out.FindNearest( u, w, mid_p );
    if ( dot( SafeNormal( out, u, w, 1.0 ), mid_c - mid_p ) < 0.0 )
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

    m_Sink.Init( "Sink", "Design", this, 1.0, 0.0, 1.0e12 );
    m_Sink.SetDescript( "Distance the fillet's face runs on past its contacts, along the wing and fuselage and easing under them" );

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
    m_Sink *= currentScale;
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

// The fillet closes itself, coming to a point at each end, so it never takes end caps.
void FilletGeom::UpdateCopySurfParms()
{
    m_CapUMinOption = vsp::NO_END_CAP;
    m_CapUMaxOption = vsp::NO_END_CAP;
    m_CapUMinOption.Deactivate();
    m_CapUMaxOption.Deactivate();
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
        bool built = BuildFillet( *ws, fd, m_Radius(), m_NumStations(), m_TETaper(), m_Sink(), surf );
        if ( built )
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
