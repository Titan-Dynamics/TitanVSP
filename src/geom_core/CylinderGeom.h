//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

// CylinderGeom.h: a plain circular cylinder, set by its length and diameter (TitanVSP).
//
//////////////////////////////////////////////////////////////////////

#if !defined(VSPCYLINDERGEOM__INCLUDED_)
#define VSPCYLINDERGEOM__INCLUDED_

#include "Geom.h"

//==== Cylinder Geom ====//
// A closed, straight circular cylinder along X, from 0 to Length, with flat ends.  It is a body,
// like a Fuselage of two equal round sections, but with nothing to shape: no sections to edit, no
// blending, no skinning.  Only its length and diameter can be set.
class CylinderGeom : public Geom
{
public:
    CylinderGeom( Vehicle* vehicle_ptr );
    virtual ~CylinderGeom();

    virtual void ComputeCenter();
    virtual void ApplyScale( double currentScale );

    Parm m_Length;      // Along X.
    Parm m_Diameter;

protected:
    virtual void UpdateSurf();
};

#endif // !defined(VSPCYLINDERGEOM__INCLUDED_)
