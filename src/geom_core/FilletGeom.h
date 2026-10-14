//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

// FilletGeom.h: Rolling ball fillet between a wing or body and the body it runs into.
//
//////////////////////////////////////////////////////////////////////

#if !defined(VSPFILLETGEOM__INCLUDED_)
#define VSPFILLETGEOM__INCLUDED_

#include "Geom.h"

//==== Fillet Geom ====//
// A child of a wing or a fuselage.  It finds the fuselage the wing's root is buried in, or the
// one the fuselage runs into, and builds the fillet between the two as its own closed body: a
// ball of radius Radius is rolled along the junction, touching both, and the arc between its
// contact points is swept around it.  Round a wing's root the fillet runs from the lower TE to
// the upper; between two bodies it is a ring all the way round.  Both are left untouched, and
// it rebuilds whenever either of them changes.
class FilletGeom : public Geom
{
public:
    FilletGeom( Vehicle* vehicle_ptr );
    virtual ~FilletGeom();

    virtual xmlNodePtr EncodeXml( xmlNodePtr & node );
    virtual xmlNodePtr DecodeXml( xmlNodePtr & node );

    virtual void GetDependencyIDs( vector< string > &ids );
    virtual void ApplyScale( double currentScale );

    // The fuselage the fillet blends into, chosen automatically.
    string GetFuseID() const                { return m_FuseID; }

    // What the last build made of the two, for the GUI.
    string GetStatus() const                { return m_Status; }

    // Fuselage-like bodies: what a fillet blends into, and what it can hang off besides a wing.
    static bool IsBodyType( int type );

    Parm m_Radius;              // Rolling ball radius; 0 picks 8% of the wing's chord, or of the
                                // slimmer body's thickness between a body and another.
    IntParm m_NumStations;      // Cross-sections around the junction.
    Parm m_TETaper;             // Fraction of each side of a wing's junction over which the
                                // radius tapers to nothing at the trailing edge.
    Parm m_Sink;                // How far the fillet's face runs on past where it touches each
                                // surface, along it and easing under it, so the fillet fades
                                // into both instead of ending on a line.

protected:
    virtual void UpdateSurf();
    virtual void UpdateCopyXFormParms();
    virtual void UpdateCopySurfParms();

    void UpdateWingSurf( Geom* wing, Geom* fuse );
    void UpdateBodySurf( Geom* body, Geom* fuse );

    void SetFuse( const string & id );
    Geom* FindFuse( Geom* parent );

    string m_FuseID;
    string m_Status;
};

#endif // !defined(VSPFILLETGEOM__INCLUDED_)
