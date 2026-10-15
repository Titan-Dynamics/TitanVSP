//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

// FilletGeom.cpp: Rolling ball fillet between a wing or body and the body it runs into.
//
// The fillet is built separately for each of the parent's surfaces (a symmetric wing has two),
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
// Between a body and another -- a pod, canopy or boom running into a fuselage, a fuselage set into
// a wing -- the junction is a closed loop with no corner, so the fillet is a ring all the way round:
//  1. The junction.  The other's signed distance is sampled over a grid on the smaller one's
//     surface, and the loops where it changes sign are traced and polished onto the junction.
//     A body passing right through another meets it in two loops, and gets two rings.
//  2. The balls.  Each is found from its distances to the two, so it rolls smoothly over a crease
//     in either, such as a seam where a fuselage's sections close on themselves at a slight
//     angle.  On a wing, the contact is held to the wing's own parameters, as round a wing root.
//     From one ball at the junction the march follows the balls' centres -- a smooth curve even
//     where the junction turns sharply, as where a fuselage's tail sinks into a wing -- all the
//     way round and back to it, in steps that shrink wherever the balls turn quickly.
//  3. The sections, as for a wing, at the full radius everywhere.
//  4. The surface.  The sections are skinned C2 round the loop, and closed on themselves.
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
#include <functional>
#include <map>

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
        double u, w;
        return Eval( p, q, n, u, w );
    }

    // As above, and ( qu, qw ) is where q is on its surface.
    double Eval( const vec3d & p, vec3d & q, vec3d & n, double & qu, double & qw )
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
                qu = u;
                qw = w;
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

bool Overlap( const BndBox & a, const BndBox & b )
{
    for ( int k = 0; k < 3; k++ )
    {
        if ( a.GetMax( k ) < b.GetMin( k ) || b.GetMax( k ) < a.GetMin( k ) )
        {
            return false;
        }
    }
    return true;
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
// A is the wing -- or, between two bodies, the one the junction was traced on -- and B the body
// it runs into.
struct Station
{
    double m_S = 0.0;       // Where the station is along the junction: the wing's chordwise
                            // parameter, or the distance round a loop between two bodies.
    double m_R = 0.0;       // Ball radius here.
    double m_UA = 0.0;      // The contact on A is at ( m_UA, m_WA ).
    double m_WA = 0.0;
    double m_UB = 0.0;      // The contact on B is at ( m_UB, m_WB ).
    double m_WB = 0.0;
    bool m_Degenerate = false;  // No ball: the section is the junction point itself.
    vec3d m_C, m_QA, m_QB, m_J;
    vec3d m_NA, m_NB;       // Outward normals of A and B at the contacts.
};

//==== A surface, and the same surface pushed out along its normal ====//
// Surfaces must only be evaluated inside their parameter ranges.  A closed body may wrap round
// in w; everything else is held at its ends.
class OffsetSide
{
public:
    OffsetSide( const VspSurf & surf, double orient, bool wrap ) :
        m_Surf( surf ), m_Orient( orient ), m_Wrap( wrap && surf.IsClosedW() )
    {
    }

    double U( double u ) const  { return std::min( std::max( u, 0.0 ), m_Surf.GetUMax() ); }
    double W( double w ) const
    {
        const double wmax = m_Surf.GetWMax();
        if ( m_Wrap )
        {
            w = fmod( w, wmax );
            return w < 0.0 ? w + wmax : w;
        }
        return std::min( std::max( w, 0.0 ), wmax );
    }
    double UMax() const         { return m_Surf.GetUMax(); }
    double WMax() const         { return m_Surf.GetWMax(); }

    vec3d Pnt( double u, double w ) const               { return m_Surf.CompPnt( U( u ), W( w ) ); }
    vec3d Normal( double u, double w ) const            { return SafeNormal( m_Surf, U( u ), W( w ), m_Orient ); }
    vec3d Off( double u, double w, double r ) const     { return Pnt( u, w ) + Normal( u, w ) * r; }

    // Derivative of Off along u ( dir 0 ) or w ( dir 1 ): central differences, one sided where
    // the parameter is at the end of its range.
    vec3d DOff( double u, double w, double r, int dir ) const
    {
        const double h = 1.0e-6;
        if ( dir == 0 )
        {
            double u0 = U( u - h ), u1 = U( u + h );
            return ( Off( u1, w, r ) - Off( u0, w, r ) ) / ( u1 - u0 );
        }
        double w0 = m_Wrap ? w - h : W( w - h );
        double w1 = m_Wrap ? w + h : W( w + h );
        return ( Off( u, w1, r ) - Off( u, w0, r ) ) / ( w1 - w0 );
    }

    // The point on the surface nearest p, searching from ( u, w ), which are updated to where it
    // is.  n is the outward normal there.
    vec3d On( const vec3d & p, double & u, double & w, vec3d & n ) const
    {
        double u0 = U( u ), w0 = W( w );
        m_Surf.FindNearest( u, w, p, u0, w0 );
        if ( !std::isfinite( u ) || !std::isfinite( w ) )
        {
            u = u0;
            w = w0;
        }
        u = U( u );
        w = W( w );
        n = Normal( u, w );
        return Pnt( u, w );
    }

    const VspSurf & m_Surf;

private:
    double m_Orient;
    bool m_Wrap;
};

//==== The two surfaces pushed out along their normals by the ball radius ====//
// A ball of radius r touching both has its centre where the two offset surfaces meet, so the
// fillet's spine is their intersection.  Solving on the surfaces' own parameters keeps each
// contact on its own patch of surface, so it moves smoothly from one station to the next --
// unlike nearest point queries, which jump between the upper and lower surfaces of a thin wing.
class OffsetPair
{
public:
    OffsetPair( const OffsetSide & a, const OffsetSide & b ) : m_A( a ), m_B( b )
    {
        BndBox bb;
        b.m_Surf.GetBoundingBox( bb );
        m_Tol = 1.0e-9 * bb.DiagDist();
    }

    // Newton for the ball of radius r touching A on its line of constant w -- a wing's chordwise
    // line: solves A( ua, wa ) = B( ub, wb ) for ua, ub and wb, starting from the values passed in.
    bool Solve( double wa, double r, double & ua, double & ub, double & wb ) const
    {
        if ( !std::isfinite( ua ) || !std::isfinite( ub ) || !std::isfinite( wb ) || !std::isfinite( wa ) || !std::isfinite( r ) )
        {
            return false;
        }

        for ( int it = 0; it < 40; it++ )
        {
            vec3d g = m_A.Off( ua, wa, r ) - m_B.Off( ub, wb, r );
            if ( !Finite( g ) )
            {
                return false;
            }
            if ( g.mag() < m_Tol )
            {
                return true;
            }

            vec3d a = m_A.DOff( ua, wa, r, 0 );
            vec3d b = m_B.DOff( ub, wb, r, 0 ) * -1.0;
            vec3d c = m_B.DOff( ub, wb, r, 1 ) * -1.0;

            double det = dot( a, cross( b, c ) );
            if ( !std::isfinite( det ) || fabs( det ) < 1.0e-300 )
            {
                return false;
            }
            vec3d rhs = g * -1.0;
            double dua = dot( rhs, cross( b, c ) ) / det;
            double dub = dot( a, cross( rhs, c ) ) / det;
            double dwb = dot( a, cross( b, rhs ) ) / det;

            // Keep each step small next to the surfaces' parameter ranges.
            double s = 1.0;
            s = std::min( s, 0.05 * m_A.UMax() / std::max( fabs( dua ), 1.0e-300 ) );
            s = std::min( s, 0.05 * m_B.UMax() / std::max( fabs( dub ), 1.0e-300 ) );
            s = std::min( s, 0.05 * m_B.WMax() / std::max( fabs( dwb ), 1.0e-300 ) );

            ua = m_A.U( ua + s * dua );
            ub = m_B.U( ub + s * dub );
            wb = m_B.W( wb + s * dwb );
        }
        return ( m_A.Off( ua, wa, r ) - m_B.Off( ub, wb, r ) ).mag() < 1.0e3 * m_Tol;
    }

    OffsetSide m_A;
    OffsetSide m_B;

private:
    double m_Tol;
};

// The ball of radius r whose centre lies in the plane through p across t and is r from both
// bodies a and b, by Newton from p.  Found from the distances to the bodies, it holds up where a
// surface's own parameters do not, such as a degenerate patch at an airfoil's nose.
bool BallInPlane( SurfDist & a, SurfDist & b, const vec3d & p, const vec3d & t, double r, Station & s )
{
    vec3d e1 = fabs( t.x() ) < 0.9 ? vec3d( 1.0, 0.0, 0.0 ) : vec3d( 0.0, 1.0, 0.0 );
    e1 = e1 - t * dot( e1, t );
    e1.normalize();
    vec3d e2 = cross( t, e1 );
    e2.normalize();

    vec3d c = p;
    for ( int it = 0; it < 40; it++ )
    {
        vec3d qa, qb, na, nb;
        double ua, wa, ub, wb;
        double da = a.Eval( c, qa, na, ua, wa );
        double db = b.Eval( c, qb, nb, ub, wb );
        vec3d ga = ( c - qa ) * ( da < 0.0 ? -1.0 : 1.0 );
        vec3d gb = ( c - qb ) * ( db < 0.0 ? -1.0 : 1.0 );
        if ( ga.mag() < 1.0e-12 * r || gb.mag() < 1.0e-12 * r )
        {
            return false;
        }
        ga.normalize();
        gb.normalize();

        double ra = da - r, rb = db - r;
        if ( fabs( ra ) + fabs( rb ) < 1.0e-7 * r )
        {
            s.m_C = c;
            s.m_QA = qa;
            s.m_QB = qb;
            s.m_UA = ua;
            s.m_WA = wa;
            s.m_UB = ub;
            s.m_WB = wb;
            s.m_NA = ga;
            s.m_NB = gb;
            return true;
        }

        double a11 = dot( ga, e1 ), a12 = dot( ga, e2 );
        double a21 = dot( gb, e1 ), a22 = dot( gb, e2 );
        double det = a11 * a22 - a12 * a21;
        if ( !std::isfinite( det ) || fabs( det ) < 1.0e-12 )
        {
            return false;
        }
        double x = ( -ra * a22 + rb * a12 ) / det;
        double y = ( -rb * a11 + ra * a21 ) / det;
        double step = sqrt( x * x + y * y );
        if ( step > 0.5 * r )
        {
            x *= 0.5 * r / step;
            y *= 0.5 * r / step;
        }
        c = c + e1 * x + e2 * y;
    }
    return false;
}

// Largest change in direction, from the ball's centre, of either contact between two stations.
double TurnAngle( const Station & a, const Station & b )
{
    if ( a.m_Degenerate || b.m_Degenerate )
    {
        return 0.0;
    }
    vec3d aw = a.m_QA - a.m_C, bw = b.m_QA - b.m_C;
    vec3d af = a.m_QB - a.m_C, bf = b.m_QB - b.m_C;
    aw.normalize();
    bw.normalize();
    af.normalize();
    bf.normalize();
    double cw = std::min( std::max( dot( aw, bw ), -1.0 ), 1.0 );
    double cf = std::min( std::max( dot( af, bf ), -1.0 ), 1.0 );
    return std::max( acos( cw ), acos( cf ) );
}

// Skins the fillet through its stations, in order along the junction.  A closed fillet -- a ring
// round a loop between two bodies -- runs on from its last station round to its first.  buried
// says whether point p, past junction point j, is buried in both bodies.
bool SkinStations( const vector< Station > & stations, const OffsetPair & op, double R, double sink,
                   bool closed, const std::function< bool( const vec3d &, const vec3d & ) > & buried, VspSurf & out )
{
    const int n = (int)stations.size();
    if ( n < 5 )
    {
        return false;
    }

    //==== One closed section per station ====//
    // The fillet's face -- carried on past the contact with A along A, round the arc, and on past
    // the contact with B along B -- then closed through a point past the junction, buried in both.
    const int narc = 16;
    const int next = 3;                     // Points on each extension of the face.
    const int nface = 2 * next + narc + 1;
    vector< double > param;
    for ( int j = next; j >= 1; j-- )
    {
        param.push_back( 0.5 * ( next - j ) / next );   // Along A, over [0, 0.5) ...
    }
    for ( int k = 0; k <= narc; k++ )
    {
        param.push_back( 0.5 + 2.0 * k / narc );        // ... the arc, over [0.5, 2.5] ...
    }
    for ( int j = 1; j <= next; j++ )
    {
        param.push_back( 2.5 + 0.5 * j / next );        // ... along B, to 3.
    }
    param.push_back( 3.5 );                             // Buried point.
    param.push_back( 4.0 );                             // Closes back on the start.

    // Sink widens the fillet: its face runs on past each contact along the surface, easing under
    // it, so it fades into both bodies instead of ending on a line.  The face dips to this
    // fraction of the run below the surface by its end.  The run shrinks with the ball toward a
    // wing's TE, and is kept within reason of the ball's size.
    const double dip = 0.15;
    const double sink0 = std::min( std::max( sink, 0.0 ), 4.0 * R );

    // How far past the junction each section closes, buried in both bodies.  A big ball next to a
    // thin body -- a slim fuselage under a large fillet -- would put a point that far out the
    // other side, and the section's closing edges would show; so it comes back toward the
    // junction until it is buried.  Then it is eased from station to station -- the least of its
    // neighbours', averaged -- since a sudden change in one section's shape, even out of sight,
    // makes the skin overshoot and fold the visible face.
    vector< double > depth( n, 0.0 );
    for ( int i = 0; i < n; i++ )
    {
        const Station & s = stations[i];
        if ( s.m_Degenerate )
        {
            continue;
        }
        vec3d in = s.m_J - s.m_C;
        in.normalize();
        depth[i] = 0.3 * s.m_R + dip * sink0 * s.m_R / R;
        for ( int k = 0; k < 16 && !buried( s.m_J, s.m_J + in * depth[i] ); k++ )
        {
            depth[i] *= 0.5;
        }
    }
    {
        const int reach = 2;
        auto at = [&]( const vector< double > & d, int j )
        {
            if ( closed )
            {
                return d[( ( j % n ) + n ) % n];
            }
            return d[std::min( std::max( j, 0 ), n - 1 )];
        };
        vector< double > least( n ), eased( n );
        for ( int i = 0; i < n; i++ )
        {
            least[i] = depth[i];
            for ( int j = i - reach; j <= i + reach; j++ )
            {
                if ( !stations[( ( j % n ) + n ) % n].m_Degenerate || !closed )
                {
                    double d = at( depth, j );
                    if ( d > 0.0 )
                    {
                        least[i] = std::min( least[i], d );
                    }
                }
            }
        }
        for ( int i = 0; i < n; i++ )
        {
            double sum = 0.0;
            int cnt = 0;
            for ( int j = i - reach; j <= i + reach; j++ )
            {
                double d = at( least, j );
                if ( d > 0.0 )
                {
                    sum += d;
                    cnt++;
                }
            }
            eased[i] = cnt > 0 ? std::min( depth[i], sum / cnt ) : depth[i];
        }
        depth.swap( eased );
    }

    vector< VspCurve > ribs( n );
    vector< vec3d > centroid( n );
    bool any_ball = false;
    for ( int i = 0; i < n; i++ )
    {
        const Station & s = stations[i];
        vector< vec3d > pts( nface + 1, s.m_J );
        if ( !s.m_Degenerate )
        {
            vec3d va = s.m_QA - s.m_C;
            vec3d vb = s.m_QB - s.m_C;
            va.normalize();
            vb.normalize();
            double ang = acos( std::min( std::max( dot( va, vb ), -1.0 ), 1.0 ) );
            for ( int k = 0; k <= narc; k++ )
            {
                double t = (double)k / narc;
                vec3d v = va;
                if ( ang > 1.0e-9 )
                {
                    v = va * ( sin( ( 1.0 - t ) * ang ) / sin( ang ) ) + vb * ( sin( t * ang ) / sin( ang ) );
                }
                pts[next + k] = s.m_C + v * s.m_R;
            }

            // The directions the arc leaves its two ends, carried on along each surface.
            double sk = sink0 * s.m_R / R;
            vec3d pn = cross( va, vb );
            pn.normalize();
            vec3d ta = cross( va, pn );     // At the contact with A, away from the arc.
            vec3d tb = cross( pn, vb );     // At the contact with B, away from the arc.

            double ua = s.m_UA, wa = s.m_WA, ub = s.m_UB, wb = s.m_WB;
            for ( int j = 1; j <= next; j++ )
            {
                double a = (double)j / next;
                vec3d nrm;
                vec3d p = op.m_A.On( s.m_QA + ta * ( sk * a ), ua, wa, nrm );
                pts[next - j] = ( ang > 1.0e-9 ) ? p - nrm * ( dip * sk * a * a ) : s.m_QA;
                p = op.m_B.On( s.m_QB + tb * ( sk * a ), ub, wb, nrm );
                pts[next + narc + j] = ( ang > 1.0e-9 ) ? p - nrm * ( dip * sk * a * a ) : s.m_QB;
            }

            // Close the section through a point past the junction, buried in both bodies.
            vec3d in = s.m_J - s.m_C;
            in.normalize();
            pts[nface] = s.m_J + in * depth[i];
            any_ball = true;
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

    if ( !any_ball )
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

    out = VspSurf();
    int first = 0, count = n - 1;
    if ( !closed )
    {
        double uscale = ( n - 1 ) / uparam.back();
        for ( double & u : uparam )
        {
            u *= uscale;
        }
        out.SkinCX( ribs, rib_data_type::C2, uparam, false );
    }
    else
    {
        // Skinning cannot close a surface on itself.  Skin the ring with a few of its sections
        // repeated past each end and keep the middle: its two ends are then the same section, and
        // the seam is as smooth as the rest to within a hair.
        double len = uparam.back() + std::max( dist( centroid[0], centroid[n - 1] ), 1.0e-9 * R );
        double uscale = n / len;
        int pad = std::min( 6, n - 1 );
        vector< VspCurve > ext;
        vector< double > eparam;
        for ( int k = -pad; k <= n + pad; k++ )
        {
            int i = ( ( k % n ) + n ) % n;
            int lap = ( k - i ) / n;
            ext.push_back( ribs[i] );
            eparam.push_back( ( uparam[i] + lap * len ) * uscale );
        }
        out.SkinCX( ext, rib_data_type::C2, eparam, false );
        first = pad;
        count = n;
    }

    // Tessellation expects each section of a surface to span exactly one unit of u; with the
    // spacing above, a short section could be given no rows at all, leaving a pinhole in the
    // mesh.  Relabel every section to span one unit, keeping only those wanted.  Each patch keeps
    // its exact shape -- only its parameter interval changes.
    piecewise_surface_type* ps = out.GetBezierSurface();
    if ( ps->number_u_patches() < first + count )
    {
        return false;
    }
    const int nvp = ps->number_v_patches();
    vector< double > dus( count, 1.0 ), dvs( nvp );
    for ( int vi = 0; vi < nvp; vi++ )
    {
        dvs[vi] = ps->get_dv( vi );
    }
    piecewise_surface_type relabeled;
    relabeled.init_uv( dus.begin(), dus.end(), dvs.begin(), dvs.end(), 0.0, ps->get_v0() );
    for ( int ui = 0; ui < count; ui++ )
    {
        for ( int vi = 0; vi < nvp; vi++ )
        {
            piecewise_surface_type::surface_type patch;
            ps->get( patch, first + ui, vi );
            relabeled.set( patch, ui, vi );
        }
    }
    *ps = relabeled;
    out.SetSurfType( vsp::NORMAL_SURF );
    out.SetMagicVParm( false );

    // The fillet's face looks out into the space the ball rolled through.  Every station votes,
    // from the middle of its arc, where section i lies at u = i.
    int keep = 0, flip = 0;
    for ( int i = 0; i < n; i++ )
    {
        const Station & s = stations[i];
        if ( s.m_Degenerate )
        {
            continue;
        }
        const double wmid = param[next + narc / 2];
        double d = dot( out.CompNorm( i, wmid ), s.m_C - out.CompPnt( i, wmid ) );
        if ( d > 0.0 )
        {
            keep++;
        }
        else if ( d < 0.0 )
        {
            flip++;
        }
    }
    if ( flip > keep )
    {
        out.FlipNormal();
    }
    return true;
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

        OffsetPair op( OffsetSide( wsurf, wing.Orient( 0 ), false ), OffsetSide( *fuse.Surf( ifuse ), fuse.Orient( ifuse ), true ) );
        if ( op.Solve( ww, radius_at( ww ), uw, uf, wf ) )
        {
            seed.m_S = ww;
            seed.m_WA = ww;
            seed.m_R = radius_at( ww );
            seed.m_UA = uw;
            seed.m_UB = uf;
            seed.m_WB = wf;
            seeded = true;
            break;
        }
    }
    if ( !seeded )
    {
        return false;
    }

    OffsetPair op( OffsetSide( wsurf, wing.Orient( 0 ), false ), OffsetSide( *fuse.Surf( ifuse ), fuse.Orient( ifuse ), true ) );

    // Fill in a solved station, and check its ball clears both bodies everywhere, not only
    // where it touches them.
    auto finish = [&]( Station & s )
    {
        s.m_C = op.m_A.Off( s.m_UA, s.m_WA, s.m_R );
        s.m_QA = op.m_A.Pnt( s.m_UA, s.m_WA );
        s.m_QB = op.m_B.Pnt( s.m_UB, s.m_WB );
        s.m_NA = op.m_A.Normal( s.m_UA, s.m_WA );
        s.m_NB = op.m_B.Normal( s.m_UB, s.m_WB );
        if ( !Finite( s.m_C ) || !Finite( s.m_QA ) || !Finite( s.m_QB ) || !Finite( s.m_NA ) || !Finite( s.m_NB ) )
        {
            return false;
        }
        wing.Reset();
        fuse.Reset();
        if ( wing( s.m_C ) < 0.98 * s.m_R || fuse( s.m_C ) < 0.98 * s.m_R )
        {
            return false;
        }
        return junction( s.m_S, s.m_J );
    };

    // Solve at w starting from 'from', walking there in smaller steps if a big one fails.
    auto solve_at = [&]( const Station & from, double w, Station & s )
    {
        Station cur = from;
        double wcur = from.m_S;
        double step = w - wcur;
        while ( fabs( w - wcur ) > 1.0e-12 )
        {
            if ( fabs( step ) < 1.0e-6 * wmax )
            {
                return false;
            }
            double wt = fabs( step ) >= fabs( w - wcur ) ? w : wcur + step;
            double uw = cur.m_UA, uf = cur.m_UB, wf = cur.m_WB;
            if ( op.Solve( wt, radius_at( wt ), uw, uf, wf ) )
            {
                cur.m_S = wt;
                cur.m_WA = wt;
                cur.m_R = radius_at( wt );
                cur.m_UA = uw;
                cur.m_UB = uf;
                cur.m_WB = wf;
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
        s.m_S = w;
        s.m_WA = w;
        s.m_Degenerate = true;
        if ( !junction( w, s.m_J ) )
        {
            return false;
        }
        s.m_C = s.m_QA = s.m_QB = s.m_J;
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
            if ( ( dir > 0 && w > seed.m_S + 1.0e-9 ) || ( dir < 0 && w < seed.m_S - 1.0e-9 ) )
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

    std::sort( stations.begin(), stations.end(), []( const Station & a, const Station & b ) { return a.m_S < b.m_S; } );

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
            if ( TurnAngle( a, b ) > max_turn && b.m_S - a.m_S > 1.0e-4 * wmax )
            {
                Station m;
                if ( solve_at( a, 0.5 * ( a.m_S + b.m_S ), m ) && finish( m ) )
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

    //==== Drop stations that step backwards ====//
    // Round the LE of a wing whose nose is much smaller than the ball, the ball hardly moves while
    // the chordwise lines sweep round the nose, and tiny wobbles in where it sits -- or where the
    // junction is -- take some stations a step back.  Skinned through, those fold the fillet
    // over on itself.  Each station must move on from the one before it the same way the next
    // one does, in its centre, both contacts and junction point; stations that don't, or that
    // all but repeat the one before, are dropped.
    for ( bool dropped = true; dropped && stations.size() > 3; )
    {
        dropped = false;
        for ( size_t i = 1; i + 1 < stations.size(); i++ )
        {
            const Station & a = stations[i - 1];
            const Station & b = stations[i];
            const Station & c = stations[i + 1];
            if ( a.m_Degenerate || b.m_Degenerate || c.m_Degenerate )
            {
                continue;
            }
            const vec3d * pa[4] = { &a.m_C, &a.m_QA, &a.m_QB, &a.m_J };
            const vec3d * pb[4] = { &b.m_C, &b.m_QA, &b.m_QB, &b.m_J };
            const vec3d * pc[4] = { &c.m_C, &c.m_QA, &c.m_QB, &c.m_J };
            bool back = false, still = true;
            for ( int k = 0; k < 4; k++ )
            {
                vec3d ac = *pc[k] - *pa[k];
                if ( ac.mag() > 1.0e-6 * R &&
                     ( dot( *pb[k] - *pa[k], ac ) < 0.0 || dot( *pc[k] - *pb[k], ac ) < 0.0 ) )
                {
                    back = true;
                }
                still = still && dist( *pb[k], *pa[k] ) < 1.0e-3 * R;
            }
            if ( back || still )
            {
                stations.erase( stations.begin() + i );
                dropped = true;
                break;
            }
        }
    }

    //==== Fill gaps the ball swings across ====//
    // Round a sharp LE a ball much bigger than the nose swings round it within a sliver of the
    // chordwise lines -- some of them on the airfoil's degenerate nose patch, where they cannot
    // be solved -- so it can still turn a long way between stations, and the fillet folds across
    // the gap.  Fill such gaps with balls held in planes across the way their centres go, found
    // from the distances to the wing and fuselage.
    {
        vector< Station > filled;
        for ( size_t i = 0; i < stations.size(); i++ )
        {
            filled.push_back( stations[i] );
            if ( i + 1 == stations.size() )
            {
                break;
            }
            const Station & a = stations[i];
            const Station & b = stations[i + 1];
            double turn = TurnAngle( a, b );
            if ( turn <= max_turn || a.m_Degenerate || b.m_Degenerate )
            {
                continue;
            }
            const int nfill = std::min( (int)std::ceil( turn / max_turn ) - 1, 60 );
            vector< Station > fill;
            for ( int k = 1; k <= nfill; k++ )
            {
                // Between the two centres, in the plane across the line joining them.  Pushed out
                // of the chord to where a ball swinging round between them would be.
                double f = (double)k / ( nfill + 1 );
                vec3d t = b.m_C - a.m_C;
                t.normalize();
                vec3d p = a.m_C + ( b.m_C - a.m_C ) * f;
                Station m = ( f < 0.5 ) ? a : b;
                m.m_R = a.m_R + ( b.m_R - a.m_R ) * f;
                m.m_S = a.m_S + ( b.m_S - a.m_S ) * f;
                m.m_J = a.m_J + ( b.m_J - a.m_J ) * f;

                // The ball solved there is good if it touches between where its neighbours do.
                // Next to a thin wing a big ball can be all but as close to another part of the
                // wing, and the nearest point jumps there; then the ball between is made up from
                // its neighbours' contacts instead, still of the full radius.
                auto between = [&]( const vec3d & q, const vec3d & q0, const vec3d & q1 )
                {
                    vec3d d = q1 - q0;
                    double s = d.mag() > 0.0 ? std::min( std::max( dot( q - q0, d ) / dot( d, d ), 0.0 ), 1.0 ) : 0.0;
                    return dist( q, q0 + d * s ) <= 0.25 * d.mag() + 0.02 * m.m_R;
                };
                Station solved = m;
                bool ok = BallInPlane( wing, fuse, p, t, m.m_R, solved ) &&
                          between( solved.m_QA, a.m_QA, b.m_QA ) && between( solved.m_QB, a.m_QB, b.m_QB );
                if ( ok )
                {
                    wing.Reset();
                    fuse.Reset();
                    ok = Finite( solved.m_C ) && wing( solved.m_C ) >= 0.98 * m.m_R && fuse( solved.m_C ) >= 0.98 * m.m_R;
                }
                if ( ok )
                {
                    m = solved;
                    m.m_S = a.m_S + ( b.m_S - a.m_S ) * f;
                    m.m_J = a.m_J + ( b.m_J - a.m_J ) * f;
                }
                else
                {
                    m.m_QA = a.m_QA + ( b.m_QA - a.m_QA ) * f;
                    m.m_QB = a.m_QB + ( b.m_QB - a.m_QB ) * f;
                    vec3d mid = ( m.m_QA + m.m_QB ) * 0.5;
                    vec3d axis = m.m_QB - m.m_QA;
                    double half = 0.5 * axis.mag();
                    if ( half >= m.m_R || half <= 0.0 )
                    {
                        fill.clear();
                        break;
                    }
                    axis.normalize();
                    vec3d out = p - mid;
                    out = out - axis * dot( out, axis );
                    out.normalize();
                    m.m_C = mid + out * sqrt( m.m_R * m.m_R - half * half );
                    m.m_NA = m.m_C - m.m_QA;
                    m.m_NB = m.m_C - m.m_QB;
                    m.m_NA.normalize();
                    m.m_NB.normalize();
                }
                fill.push_back( m );
            }
            filled.insert( filled.end(), fill.begin(), fill.end() );
        }
        stations.swap( filled );
    }

    // Close off both ends at the TE in a point.
    for ( double w : { wtip, wmax - wtip } )
    {
        bool have = false;
        for ( const Station & s : stations )
        {
            have = have || fabs( s.m_S - w ) < 1.0e-9;
        }
        Station s;
        if ( !have && degenerate_at( w, s ) )
        {
            stations.push_back( s );
        }
    }
    std::sort( stations.begin(), stations.end(), []( const Station & a, const Station & b ) { return a.m_S < b.m_S; } );

    // Buried enough: no farther outside either body than the junction point itself, give or take a
    // hair.  The junction is not always exactly on both -- by a sharp edge, say -- and a point
    // that is in as far as the junction is hides the section's closing edges as well as can be.
    auto buried = [&]( const vec3d & j, const vec3d & p )
    {
        const double tol = 0.01 * R;
        wing.Reset();
        fuse.Reset();
        double wj = std::max( wing( j ), 0.0 ), fj = std::max( fuse( j ), 0.0 );
        wing.Reset();
        fuse.Reset();
        return wing( p ) <= wj + tol && fuse( p ) <= fj + tol;
    };
    return SkinStations( stations, op, R, sink, false, buried, out );
}

//==== Between two bodies ====//

// The closed loops where body a passes into body b, found on a grid over a's surface and
// polished onto the junction.  bbox bounds b.  Loops that run off the edge of a's parameters,
// as round an open end, are dropped.
void JunctionLoops( const VspSurf & a, SurfDist & b, const BndBox & bbox, vector< vector< vec3d > > & loops )
{
    const double umax = a.GetUMax(), wmax = a.GetWMax();
    const bool wrap = a.IsClosedW();
    const int nu = std::max( 64, 16 * (int)std::ceil( umax ) );
    const int nw = 64;
    const int nj = wrap ? nw : nw + 1;      // Columns of nodes; a closed surface's last is its first.

    auto uat = [&]( int i ) { return umax * i / nu; };
    auto wat = [&]( int j ) { return wmax * j / nw; };

    // Which side of b each node is on.  Outside b's box is outside b, which saves finding the
    // distance to most of a.
    BndBox box = bbox;
    box.Expand( 1.0e-3 * bbox.DiagDist() );
    vector< double > f( ( nu + 1 ) * nj, 1.0 );
    auto node = [&]( int i, int j ) { return f[i * nj + j % nj]; };
    b.Reset();
    for ( int i = 0; i <= nu; i++ )
    {
        for ( int j = 0; j < nj; j++ )
        {
            vec3d p = a.CompPnt( uat( i ), wat( j ) );
            if ( box.CheckPnt( p ) )
            {
                f[i * nj + j] = b( p );
            }
        }
    }

    // The junction crosses grid edges: edge ( i, j, 0 ) runs along u from node ( i, j ), and
    // edge ( i, j, 1 ) along w.  In each cell it crosses, it joins two of the cell's edges.
    auto edge_id = [&]( int i, int j, int dir ) { return ( i * nj + j % nj ) * 2 + dir; };
    std::map< int, vector< int > > links;
    auto link = [&]( int e0, int e1 )
    {
        links[e0].push_back( e1 );
        links[e1].push_back( e0 );
    };
    for ( int i = 0; i < nu; i++ )
    {
        for ( int j = 0; j < nw; j++ )
        {
            const int jr = j + 1;
            bool ia = node( i, j ) < 0.0, ib = node( i + 1, j ) < 0.0;
            bool ic = node( i + 1, jr ) < 0.0, id = node( i, jr ) < 0.0;
            // Corners a ( i, j ), b ( i + 1, j ), c ( i + 1, j + 1 ), d ( i, j + 1 ); edges
            // a-b, b-c, d-c and a-d.
            int e[4] = { edge_id( i, j, 0 ), edge_id( i + 1, j, 1 ), edge_id( i, jr, 0 ), edge_id( i, j, 1 ) };
            bool cut[4] = { ia != ib, ib != ic, id != ic, ia != id };
            vector< int > cuts;
            for ( int k = 0; k < 4; k++ )
            {
                if ( cut[k] )
                {
                    cuts.push_back( e[k] );
                }
            }
            if ( cuts.size() == 2 )
            {
                link( cuts[0], cuts[1] );
            }
            else if ( cuts.size() == 4 )
            {
                // A saddle: the middle of the cell says which corners the junction cuts off.
                bool mid = b( a.CompPnt( 0.5 * ( uat( i ) + uat( i + 1 ) ), 0.5 * ( wat( j ) + wat( jr ) ) ) ) < 0.0;
                if ( mid == ia )
                {
                    link( e[0], e[1] );
                    link( e[2], e[3] );
                }
                else
                {
                    link( e[0], e[3] );
                    link( e[1], e[2] );
                }
            }
        }
    }

    // Where the junction crosses an edge, found by regula falsi along it.
    BndBox abox;
    a.GetBoundingBox( abox );
    const double tol = 1.0e-9 * std::max( abox.DiagDist(), bbox.DiagDist() );
    auto crossing = [&]( int id )
    {
        int dir = id % 2, k = id / 2, i = k / nj, j = k % nj;
        double u0 = uat( i ), w0 = wat( j ), u1 = u0, w1 = w0;
        if ( dir == 0 )
        {
            u1 = uat( i + 1 );
        }
        else
        {
            w1 = wat( j + 1 );
        }
        double t0 = 0.0, t1 = 1.0;
        double f0 = b( a.CompPnt( u0, w0 ) ), f1 = b( a.CompPnt( u1, w1 ) );
        vec3d p = a.CompPnt( u0, w0 );
        int side = 0;
        for ( int it = 0; it < 60; it++ )
        {
            double t = ( f0 != f1 ) ? ( f0 * t1 - f1 * t0 ) / ( f0 - f1 ) : 0.5 * ( t0 + t1 );
            t = std::min( std::max( t, t0 ), t1 );
            p = a.CompPnt( u0 + t * ( u1 - u0 ), w0 + t * ( w1 - w0 ) );
            double fp = b( p );
            if ( fabs( fp ) < tol || t1 - t0 < 1.0e-12 )
            {
                break;
            }
            // Illinois: halve the end that keeps being kept, so the bracket closes from both sides.
            if ( ( fp < 0.0 ) == ( f0 < 0.0 ) )
            {
                t0 = t;
                f0 = fp;
                if ( side == -1 )
                {
                    f1 *= 0.5;
                }
                side = -1;
            }
            else
            {
                t1 = t;
                f1 = fp;
                if ( side == 1 )
                {
                    f0 *= 0.5;
                }
                side = 1;
            }
        }
        return p;
    };

    // Follow each chain of linked edges round; only those that come back to their start are loops.
    std::map< int, bool > used;
    for ( const auto & kv : links )
    {
        if ( used[kv.first] )
        {
            continue;
        }
        vector< int > ring;
        int prev = -1, cur = kv.first;
        bool closed = false;
        while ( true )
        {
            used[cur] = true;
            ring.push_back( cur );
            const vector< int > & nb = links[cur];
            if ( nb.size() != 2 )
            {
                break;
            }
            int nxt = ( nb[0] != prev ) ? nb[0] : nb[1];
            prev = cur;
            cur = nxt;
            if ( cur == kv.first )
            {
                closed = true;
                break;
            }
            if ( used[cur] )
            {
                break;
            }
        }
        if ( !closed || ring.size() < 6 )
        {
            continue;
        }

        vector< vec3d > pts;
        for ( int id : ring )
        {
            pts.push_back( crossing( id ) );
        }
        loops.push_back( pts );
    }
}

// How thick a body is across its thickest section: a fuselage's diameter, a wing's chord.
double Thickness( const VspSurf & s )
{
    const double half = 0.5 * s.GetWMax();
    double t = 0.0;
    for ( int i = 1; i < 20; i++ )
    {
        double u = s.GetUMax() * i / 20;
        for ( int j = 0; j < 16; j++ )
        {
            double w = half * j / 16;
            t = std::max( t, dist( s.CompPnt( u, w ), s.CompPnt( u, w + half ) ) );
        }
    }
    return t;
}

// The ring fillet round one loop where body a meets b, a body or -- if bwing -- a wing.
bool BuildRing( const VspSurf & asurf, const VspSurf & bsurf, bool bwing, const vector< vec3d > & loop, double R,
                int nstation, double sink, VspSurf & out )
{
    SurfDist ad, bd;
    ad.Add( &asurf );
    bd.Add( &bsurf );

    //==== The junction, as a smooth closed curve through the loop ====//
    // Points crowding their neighbour, where the loop cut a corner of a grid cell, would kink it.
    double gap = 0.0;
    for ( size_t i = 0; i < loop.size(); i++ )
    {
        gap += dist( loop[i], loop[( i + 1 ) % loop.size()] );
    }
    gap *= 0.3 / loop.size();
    vector< vec3d > pts;
    for ( const vec3d & p : loop )
    {
        if ( pts.empty() || dist( p, pts.back() ) > gap )
        {
            pts.push_back( p );
        }
    }
    while ( pts.size() > 3 && dist( pts.back(), pts.front() ) <= gap )
    {
        pts.pop_back();
    }
    const int npt = (int)pts.size();
    if ( npt < 6 )
    {
        return false;
    }
    vector< double > par( npt + 1, 0.0 );
    for ( int i = 1; i <= npt; i++ )
    {
        par[i] = par[i - 1] + dist( pts[i - 1], pts[i % npt] );
    }
    const double L = par.back();
    VspCurve guide;
    guide.InterpolateCSpline( pts, par, true );

    // The junction point and its direction at distance s round the loop.
    auto along = [&]( double s, vec3d & p, vec3d & t )
    {
        s = fmod( s, L );
        if ( s < 0.0 )
        {
            s += L;
        }
        p = guide.CompPnt( s );
        t = guide.CompTan( s );
        t.normalize();
    };

    OffsetPair op( OffsetSide( asurf, ad.Orient( 0 ), true ), OffsetSide( bsurf, bd.Orient( 0 ), !bwing ) );

    // The ball of radius r whose centre lies in the plane through p across t and is r from both
    // bodies, by Newton from c.  Working on the distances to the bodies, not on their surfaces
    // pushed out along their normals, lets the ball roll smoothly over a crease -- a seam where a
    // fuselage's sections close on themselves at a slight angle -- where the ball rests on the
    // crease while its centre swings round it.
    auto solve_ball = [&]( const vec3d & p, const vec3d & t, double r, vec3d c, Station & s )
    {
        c = c - t * dot( c - p, t );
        vec3d e1 = c - p;
        if ( e1.mag() < 1.0e-9 * r )
        {
            e1 = fabs( t.x() ) < 0.9 ? vec3d( 1.0, 0.0, 0.0 ) : vec3d( 0.0, 1.0, 0.0 );
        }
        e1 = e1 - t * dot( e1, t );
        e1.normalize();
        vec3d e2 = cross( t, e1 );
        e2.normalize();

        double err = DBL_MAX;
        for ( int it = 0; it < 40; it++ )
        {
            vec3d qa, qb, na, nb;
            double ua, wa, ub, wb;
            double da = ad.Eval( c, qa, na, ua, wa );
            double db = bd.Eval( c, qb, nb, ub, wb );

            // Distance grows straight away from the nearest point, which on a crease need not be
            // along either side's normal.
            vec3d ga = ( c - qa ) * ( da < 0.0 ? -1.0 : 1.0 );
            vec3d gb = ( c - qb ) * ( db < 0.0 ? -1.0 : 1.0 );
            if ( ga.mag() < 1.0e-12 * r || gb.mag() < 1.0e-12 * r )
            {
                return false;
            }
            ga.normalize();
            gb.normalize();

            double ra = da - r, rb = db - r;
            err = fabs( ra ) + fabs( rb );
            s.m_C = c;
            s.m_R = r;
            s.m_QA = qa;
            s.m_QB = qb;
            s.m_UA = ua;
            s.m_WA = wa;
            s.m_UB = ub;
            s.m_WB = wb;
            s.m_NA = ga;
            s.m_NB = gb;
            if ( err < 1.0e-7 * r )
            {
                return true;
            }

            double a11 = dot( ga, e1 ), a12 = dot( ga, e2 );
            double a21 = dot( gb, e1 ), a22 = dot( gb, e2 );
            double det = a11 * a22 - a12 * a21;
            if ( !std::isfinite( det ) || fabs( det ) < 1.0e-12 )
            {
                return false;
            }
            double x = ( -ra * a22 + rb * a12 ) / det;
            double y = ( -rb * a11 + ra * a21 ) / det;
            double step = sqrt( x * x + y * y );
            if ( step > 0.5 * r )
            {
                x *= 0.5 * r / step;
                y *= 0.5 * r / step;
            }
            c = c + e1 * x + e2 * y;
        }
        return err < 1.0e-5 * r;
    };

    // The same ball against a wing, with its contact on the wing held to the wing's own
    // parameters, from ( ub, wb ).  The nearest point on a thin wing can flip between its upper
    // and lower surfaces round the LE, where the ball must roll smoothly from one to the other.
    // The centre is the wing pushed out by r there; Newton puts it r from body a, in the plane.
    auto solve_ball_wing = [&]( const vec3d & p, const vec3d & t, double r, double ub, double wb, Station & s )
    {
        double err = DBL_MAX;
        for ( int it = 0; it < 40; it++ )
        {
            vec3d c = op.m_B.Off( ub, wb, r );
            vec3d qa, na;
            double ua, wa;
            double da = ad.Eval( c, qa, na, ua, wa );
            vec3d ga = ( c - qa ) * ( da < 0.0 ? -1.0 : 1.0 );
            if ( !Finite( c ) || ga.mag() < 1.0e-12 * r )
            {
                return false;
            }
            ga.normalize();

            double f1 = da - r, f2 = dot( c - p, t );
            err = fabs( f1 ) + fabs( f2 );
            s.m_C = c;
            s.m_R = r;
            s.m_QA = qa;
            s.m_QB = op.m_B.Pnt( ub, wb );
            s.m_UA = ua;
            s.m_WA = wa;
            s.m_UB = ub;
            s.m_WB = wb;
            s.m_NA = ga;
            s.m_NB = op.m_B.Normal( ub, wb );
            if ( err < 1.0e-7 * r )
            {
                return true;
            }

            vec3d cu = op.m_B.DOff( ub, wb, r, 0 ), cw = op.m_B.DOff( ub, wb, r, 1 );
            double a11 = dot( ga, cu ), a12 = dot( ga, cw );
            double a21 = dot( t, cu ), a22 = dot( t, cw );
            double det = a11 * a22 - a12 * a21;
            if ( !std::isfinite( det ) || fabs( det ) < 1.0e-300 )
            {
                return false;
            }
            double du = ( -f1 * a22 + f2 * a12 ) / det;
            double dw = ( -f2 * a11 + f1 * a21 ) / det;

            // Keep each step small next to the wing's parameter ranges.
            double sc = 1.0;
            sc = std::min( sc, 0.05 * op.m_B.UMax() / std::max( fabs( du ), 1.0e-300 ) );
            sc = std::min( sc, 0.05 * op.m_B.WMax() / std::max( fabs( dw ), 1.0e-300 ) );
            ub = op.m_B.U( ub + sc * du );
            wb = op.m_B.W( wb + sc * dw );
        }
        return err < 1.0e-5 * r;
    };

    // The ball in plane ( p, t ), starting from station 'from' or, for the first, from centre c.
    auto solve = [&]( const vec3d & p, const vec3d & t, double r, const vec3d & c, const Station & from, Station & s )
    {
        return bwing ? solve_ball_wing( p, t, r, from.m_UB, from.m_WB, s ) : solve_ball( p, t, r, c, s );
    };

    // The junction point nearest q: the closest of a run of points along the junction, polished.
    const int ng = 400;
    vector< vec3d > gpts( ng );
    for ( int i = 0; i < ng; i++ )
    {
        gpts[i] = guide.CompPnt( L * i / ng );
    }
    auto junction_near = [&]( const vec3d & q )
    {
        int best = 0;
        double bestd = DBL_MAX;
        for ( int i = 0; i < ng; i++ )
        {
            double d = dist_squared( q, gpts[i] );
            if ( d < bestd )
            {
                bestd = d;
                best = i;
            }
        }
        double u = L * best / ng;
        guide.FindNearest( u, q, L * best / ng );
        vec3d j = guide.CompPnt( u );
        return ( Finite( j ) && dist_squared( j, q ) <= bestd ) ? j : gpts[best];
    };

    // Fill in a solved station: the junction point it fills.  And check its ball clears both
    // bodies everywhere, not only where it touches them.
    auto finish = [&]( Station & s )
    {
        s.m_J = junction_near( ( s.m_QA + s.m_QB ) * 0.5 );
        if ( !Finite( s.m_C ) || !Finite( s.m_QA ) || !Finite( s.m_QB ) || !Finite( s.m_J ) )
        {
            return false;
        }
        ad.Reset();
        bd.Reset();
        return ad( s.m_C ) >= 0.98 * s.m_R && bd( s.m_C ) >= 0.98 * s.m_R;
    };

    // The ball in the corner at junction point p, not another beyond it.  Between two flat walls
    // whose normals are an angle g apart, a ball touches each r tan( g / 2 ) from the junction,
    // and the way from the junction to its centre runs through open air.
    auto near_junction = [&]( const vec3d & p, const Station & s )
    {
        if ( dist( s.m_C, p ) > 20.0 * s.m_R )
        {
            return false;
        }
        double g = acos( std::min( std::max( dot( s.m_NA, s.m_NB ), -1.0 ), 1.0 ) );
        double reach = 2.0 * s.m_R * tan( 0.5 * std::min( g, 0.99 * M_PI ) ) + 0.5 * s.m_R;
        if ( dist( s.m_QA, p ) > reach || dist( s.m_QB, p ) > reach )
        {
            return false;
        }
        for ( double f : { 0.25, 0.5, 0.75 } )
        {
            vec3d x = p + ( s.m_C - p ) * f;
            if ( ad( x ) < -0.1 * s.m_R || bd( x ) < -0.1 * s.m_R )
            {
                return false;
            }
        }
        return true;
    };

    //==== Seed: one ball found from the junction ====//
    const int nst = std::max( nstation, 12 );
    Station seed;
    bool seeded = false;
    for ( double f : { 0.0, 0.25, 0.5, 0.75, 0.125, 0.375, 0.625, 0.875 } )
    {
        double s = f * L;
        vec3d j, t, q, na, nb;
        along( s, j, t );
        ad.Reset();
        bd.Reset();
        ad.Eval( j, q, na );
        bd.Eval( j, q, nb );

        vec3d c, qa, qb;
        Station st;
        if ( !SolveBall( ad, bd, j, t, na, nb, R, c, qa, qb ) )
        {
            continue;
        }
        size_t ib;
        bd.GlobalNearest( qb, ib, st.m_UB, st.m_WB );
        if ( solve( j, t, R, c, st, st ) && near_junction( j, st ) )
        {
            st.m_S = 0.0;
            if ( finish( st ) )
            {
                seed = st;
                seeded = true;
                break;
            }
        }
    }
    if ( !seeded )
    {
        return false;
    }

    //==== Follow the balls round, from the seed back to it ====//
    // The balls' centres trace a smooth closed curve -- the spine, where the two bodies pushed out
    // by r meet -- even where the junction itself turns sharply, as round the tail of a fuselage
    // sinking into a wing.  So the march follows the spine: a step out along its tangent, then
    // Newton back onto it across the tangent.  Steps shrink wherever the spine or the contacts
    // turn quickly, so each lands on the same ball rolled on, and grow again where they do not.
    const double hmax = L / nst;
    const double max_turn = 5.0 * M_PI / 180.0;
    auto tangent = [&]( const Station & s, const vec3d & ahead )
    {
        vec3d t = cross( s.m_NA, s.m_NB );
        t.normalize();
        return dot( t, ahead ) < 0.0 ? t * -1.0 : t;
    };

    vec3d jseed, tseed;
    along( 0.0, jseed, tseed );
    vector< Station > stations = { seed };
    Station cur = seed;
    vec3d tcur = tangent( seed, tseed );
    double h = hmax, travelled = 0.0;
    bool closed = false;
    for ( int it = 0; it < 100 * nst && travelled < 4.0 * L + 20.0 * R; it++ )
    {
        // Back round to the seed: the last stretch closes the ring.
        if ( travelled > 3.0 * hmax && dist( cur.m_C, seed.m_C ) < 1.25 * h && dot( seed.m_C - cur.m_C, tcur ) > 0.0 )
        {
            closed = true;
            break;
        }

        vec3d cp = cur.m_C + tcur * h;
        Station next = cur;
        bool ok = solve( cp, tcur, R, cp, cur, next );
        vec3d tn;
        if ( ok )
        {
            tn = tangent( next, tcur );
            ok = dist( next.m_C, cp ) < 0.25 * h + 1.0e-9 * R &&
                 dot( tn, tcur ) > cos( 2.0 * max_turn ) &&
                 TurnAngle( cur, next ) < max_turn &&
                 dist( next.m_QA, cur.m_QA ) < 4.0 * h + 0.05 * R &&
                 dist( next.m_QB, cur.m_QB ) < 4.0 * h + 0.05 * R;
        }
        if ( !ok )
        {
            h *= 0.5;
            if ( h < 1.0e-5 * R )
            {
                return false;   // Lost the ball.
            }
            continue;
        }

        travelled += h;
        next.m_S = cur.m_S + h;
        cur = next;
        tcur = tn;
        if ( finish( next ) )
        {
            stations.push_back( next );
        }
        h = std::min( 1.5 * h, hmax );
    }
    if ( !closed )
    {
        return false;
    }

    // The last section must not crowd the first, which the ring closes onto.
    while ( stations.size() > 5 && dist( stations.back().m_C, seed.m_C ) < 0.25 * hmax )
    {
        stations.pop_back();
    }

    // Buried enough: no farther outside either body than the junction point itself, give or take a
    // hair.  The junction is not always exactly on both -- by a sharp edge, say.
    auto buried = [&]( const vec3d & j, const vec3d & p )
    {
        const double tol = 0.01 * R;
        ad.Reset();
        bd.Reset();
        double aj = std::max( ad( j ), 0.0 ), bj = std::max( bd( j ), 0.0 );
        ad.Reset();
        bd.Reset();
        return ad( p ) <= aj + tol && bd( p ) <= bj + tol;
    };
    return SkinStations( stations, op, R, sink, true, buried, out );
}

// How many points of a grid over each of a's surfaces lie inside b.
int CountInside( Geom* a, Geom* b )
{
    SurfDist bd;
    BndBox bb;
    for ( int k = 0; k < b->GetNumTotalSurfs(); k++ )
    {
        const VspSurf* s = b->GetSurfPtr( k );
        if ( s )
        {
            bd.Add( s );
            BndBox sb;
            s->GetBoundingBox( sb );
            bb.Update( sb );
        }
    }
    if ( bd.Empty() )
    {
        return 0;
    }

    int nin = 0;
    for ( int i = 0; i < a->GetNumTotalSurfs(); i++ )
    {
        const VspSurf* s = a->GetSurfPtr( i );
        if ( !s )
        {
            continue;
        }
        for ( int iu = 1; iu < 24; iu++ )
        {
            for ( int iw = 0; iw < 16; iw++ )
            {
                vec3d p = s->CompPnt( s->GetUMax() * iu / 24, s->GetWMax() * iw / 16 );
                if ( bb.CheckPnt( p ) && bd( p ) < 0.0 )
                {
                    nin++;
                }
            }
        }
    }
    return nin;
}

} // namespace

//==== Constructor ====//
FilletGeom::FilletGeom( Vehicle* vehicle_ptr ) : Geom( vehicle_ptr )
{
    m_Name = "FilletGeom";
    m_Type.m_Name = "Fillet";
    m_Type.m_Type = FILLET_GEOM_TYPE;

    m_Radius.Init( "Radius", "Design", this, 0.0, 0.0, 1.0e12 );
    m_Radius.SetDescript( "Radius of the ball rolled along the junction; 0 picks 8% of the wing's chord, or of the slimmer body's thickness" );

    m_NumStations.Init( "NumStations", "Design", this, 41, 9, 401 );
    m_NumStations.SetDescript( "Number of cross-sections around the junction" );

    m_TETaper.Init( "TETaper", "Design", this, 0.75, 0.0, 1.0 );
    m_TETaper.SetDescript( "Fraction of the junction, from the TE, over which the radius tapers to nothing (wings only)" );

    m_Sink.Init( "Sink", "Design", this, 1.0, 0.0, 1.0e12 );
    m_Sink.SetDescript( "Distance the fillet's face runs on past its contacts, along both surfaces and easing under them" );

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

// Built from the wing or body it hangs off and the body it blends into.
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

// The bodies a fillet blends into, and that one can hang off besides a wing.
bool FilletGeom::IsBodyType( int type )
{
    return type == FUSELAGE_GEOM_TYPE || type == POD_GEOM_TYPE || type == STACK_GEOM_TYPE;
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

    // Each of the parent's surfaces, mirrored copies included, gets a fillet of its own.
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

// The fillet closes itself -- to a point at each end of a wing's, round on itself between two
// bodies -- so it never takes end caps.
void FilletGeom::UpdateCopySurfParms()
{
    m_CapUMinOption = vsp::NO_END_CAP;
    m_CapUMaxOption = vsp::NO_END_CAP;
    m_CapUMinOption.Deactivate();
    m_CapUMaxOption.Deactivate();

    // Only a wing root's junction has a TE to taper toward.  Between two bodies there is none.
    Geom* parent = m_Vehicle->FindGeom( m_ParentID );
    if ( parent && parent->GetType().m_Type == MS_WING_GEOM_TYPE )
    {
        m_TETaper.Activate();
    }
    else
    {
        m_TETaper = 0.0;
        m_TETaper.Deactivate();
    }
}

// Blend into a different body, rebuilding whenever that one changes.
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

// The body the parent runs into.  For a wing, the fuselage holding most of its root section; for
// a body, the fuselage or wing it overlaps most -- the wing a fuselage is set into, say.
Geom* FilletGeom::FindFuse( Geom* parent )
{
    const bool wing = parent->GetType().m_Type == MS_WING_GEOM_TYPE;
    auto candidate = [&]( Geom* g )
    {
        int type = g->GetType().m_Type;
        return g != parent && ( IsBodyType( type ) || ( !wing && type == MS_WING_GEOM_TYPE ) ) && !g->DependsOn( m_ID );
    };

    Geom* current = m_Vehicle->FindGeom( m_FuseID );
    if ( current && candidate( current ) )
    {
        return current;
    }

    const VspSurf* root = parent->GetSurfPtr( 0 );
    if ( !root )
    {
        return nullptr;
    }

    Geom* best = nullptr;
    int best_in = 0;
    for ( Geom* g : m_Vehicle->FindGeomVec( m_Vehicle->GetGeomVec() ) )
    {
        if ( !g || !candidate( g ) )
        {
            continue;
        }

        int nin = 0;
        if ( wing )
        {
            SurfDist fd;
            for ( int i = 0; i < g->GetNumTotalSurfs(); i++ )
            {
                fd.Add( g->GetSurfPtr( i ) );
            }
            if ( fd.Empty() )
            {
                continue;
            }

            for ( double w : LoopParams( 17, root->GetWMax() ) )
            {
                fd.Reset();
                if ( fd( root->CompPnt( 0.0, w ) ) < 0.0 )
                {
                    nin++;
                }
            }
        }
        else
        {
            // Either may be the smaller, and lie mostly inside the other.
            nin = CountInside( parent, g ) + CountInside( g, parent );
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

    Geom* parent = m_Vehicle->FindGeom( m_ParentID );
    const int ptype = parent ? parent->GetType().m_Type : -1;
    const bool wing = ptype == MS_WING_GEOM_TYPE;
    if ( !wing && !IsBodyType( ptype ) )
    {
        m_Status = "A fillet must be added to a wing or a fuselage.";
        return;
    }

    Geom* fuse = FindFuse( parent );
    if ( !fuse )
    {
        SetFuse( string() );
        m_Status = wing ? "The wing's root is not inside a fuselage." : parent->GetName() + " does not meet a fuselage or wing.";
        return;
    }
    SetFuse( fuse->GetID() );

    if ( wing )
    {
        UpdateWingSurf( parent, fuse );
    }
    else
    {
        UpdateBodySurf( parent, fuse );
    }
}

// A wing: one fillet for each of its surfaces, round the root from the lower TE to the upper.
void FilletGeom::UpdateWingSurf( Geom* wing, Geom* fuse )
{
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

// Two bodies: a ring round every loop where one of the parent's surfaces meets one of the
// other body's -- one for a pod, canopy or boom ending in it, two for one passing through.
void FilletGeom::UpdateBodySurf( Geom* body, Geom* fuse )
{
    struct Junction
    {
        const VspSurf* m_A;
        const VspSurf* m_B;
        bool m_BWing;
        vector< vec3d > m_Loop;
    };
    vector< Junction > junctions;
    for ( int i = 0; i < body->GetNumTotalSurfs(); i++ )
    {
        for ( int k = 0; k < fuse->GetNumTotalSurfs(); k++ )
        {
            const VspSurf* ps = body->GetSurfPtr( i );
            const VspSurf* fs = fuse->GetSurfPtr( k );
            if ( !ps || !fs )
            {
                continue;
            }
            BndBox pb, fb;
            ps->GetBoundingBox( pb );
            fs->GetBoundingBox( fb );
            if ( !Overlap( pb, fb ) )
            {
                continue;
            }

            // Trace the junction on the smaller body, whose grid resolves it better.
            const VspSurf* a = pb.DiagDist() <= fb.DiagDist() ? ps : fs;
            const VspSurf* b = ( a == ps ) ? fs : ps;
            SurfDist bd;
            bd.Add( b );
            vector< vector< vec3d > > loops;
            JunctionLoops( *a, bd, a == ps ? fb : pb, loops );
            // A wing is always B, whose contact is solved on the wing's own parameters.
            const bool fwing = fuse->GetType().m_Type == MS_WING_GEOM_TYPE;
            for ( const vector< vec3d > & loop : loops )
            {
                if ( fwing )
                {
                    junctions.push_back( { ps, fs, true, loop } );
                }
                else
                {
                    junctions.push_back( { a, b, false, loop } );
                }
            }
        }
    }

    if ( junctions.empty() )
    {
        m_Status = body->GetName() + " does not meet " + fuse->GetName() + ".";
        return;
    }

    // First build: size the ball from the slimmer of the two at its thickest -- a fuselage's
    // diameter, not the chord of the wing it sits on.
    if ( m_Radius() <= 0.0 )
    {
        double width = 0.0;
        for ( const Junction & j : junctions )
        {
            width = std::max( width, std::min( Thickness( *j.m_A ), Thickness( *j.m_B ) ) );
        }
        if ( width > 0.0 )
        {
            m_Radius.Set( 0.08 * width );
        }
        if ( m_Radius() <= 0.0 )
        {
            m_Status = "Could not size the fillet; set its radius.";
            return;
        }
    }

    int nbuilt = 0;
    for ( const Junction & j : junctions )
    {
        VspSurf surf;
        if ( BuildRing( *j.m_A, *j.m_B, j.m_BWing, j.m_Loop, m_Radius(), m_NumStations(), m_Sink(), surf ) )
        {
            m_MainSurfVec.push_back( surf );
            nbuilt++;
        }
    }

    if ( nbuilt == (int)junctions.size() )
    {
        m_Status = "Blending " + body->GetName() + " into " + fuse->GetName() + ".";
    }
    else
    {
        m_Status = "The ball does not fit all the way round " + std::to_string( junctions.size() - nbuilt ) + " of " +
                   std::to_string( junctions.size() ) + " junctions; try a smaller radius.";
    }
}
