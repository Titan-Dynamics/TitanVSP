//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

// FilletGeom.h: Rolling ball fillet between a wing and the fuselage its root is buried in.
//
//////////////////////////////////////////////////////////////////////

#if !defined(VSPFILLETGEOM__INCLUDED_)
#define VSPFILLETGEOM__INCLUDED_

#include "Geom.h"

//==== Fillet Geom ====//
// A child of a wing.  It finds the fuselage the wing's root is buried in and builds the
// fillet between the two as its own closed body: a ball of radius Radius is rolled along the
// junction, touching both, and the arc between its contact points is swept around the root.
// The wing and fuselage are left untouched.  It rebuilds whenever either of them changes.
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

    // What the last build made of the wing and fuselage, for the GUI.
    string GetStatus() const                { return m_Status; }

    Parm m_Radius;              // Rolling ball radius; 0 picks 8% of the chord at the junction.
    IntParm m_NumStations;      // Cross-sections around the junction.
    Parm m_TETaper;             // Fraction of each side of the junction over which the
                                // radius tapers to nothing at the trailing edge.
    Parm m_Sink;                // How far the fillet's edges run on into the wing and fuselage,
                                // so no sliver of gap opens along the lines where it meets them.

protected:
    virtual void UpdateSurf();
    virtual void UpdateCopyXFormParms();

    void SetFuse( const string & id );
    Geom* FindFuse( Geom* wing );

    string m_FuseID;
    string m_Status;
};

#endif // !defined(VSPFILLETGEOM__INCLUDED_)
