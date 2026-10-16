//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

// CylinderGeom.cpp: a plain circular cylinder, set by its length and diameter (TitanVSP).
//
//////////////////////////////////////////////////////////////////////

#include "CylinderGeom.h"
#include "Vehicle.h"

//==== Constructor ====//
CylinderGeom::CylinderGeom( Vehicle* vehicle_ptr ) : Geom( vehicle_ptr )
{
    m_Name = "CylinderGeom";
    m_Type.m_Name = "Cylinder";
    m_Type.m_Type = CYLINDER_GEOM_TYPE;

    m_TessU = 10;
    m_TessW = 17;

    m_Length.Init( "Length", "Design", this, 10.0, 1.0e-8, 1.0e12 );
    m_Length.SetDescript( "Length of the cylinder, along X" );
    m_Diameter.Init( "Diameter", "Design", this, 2.0, 1.0e-8, 1.0e12 );
    m_Diameter.SetDescript( "Diameter of the cylinder" );
}

//==== Destructor ====//
CylinderGeom::~CylinderGeom()
{
}

// The surface is a profile swept round the X axis: out from the axis to the radius, along the
// length, and back in.  The three straight pieces give the front face, the side and the back
// face, so the cylinder is closed with flat ends, and its two end circles are feature lines.
void CylinderGeom::UpdateSurf()
{
    double len = m_Length();
    double rad = 0.5 * m_Diameter();

    vec3d pts[4] = { vec3d( 0.0, 0.0, 0.0 ), vec3d( 0.0, 0.0, rad ), vec3d( len, 0.0, rad ), vec3d( len, 0.0, 0.0 ) };

    // Each straight piece as a cubic with its control points evenly along it.
    VspCurve stringer;
    curve_segment_type cs( 3 );
    curve_segment_type::control_point_type cp;
    for ( int i = 0; i < 3; i++ )
    {
        for ( int k = 0; k < 4; k++ )
        {
            vec3d p = pts[i] + ( pts[i + 1] - pts[i] ) * ( k / 3.0 );
            cp << p.x(), p.y(), p.z();
            cs.set_control_point( cp, k );
        }
        stringer.AppendCurveSegment( cs );
    }

    m_MainSurfVec[0].CreateBodyRevolution( stringer );
    m_MainSurfVec[0].SetMagicVParm( false );
}

//==== Compute Rotation Center ====//
void CylinderGeom::ComputeCenter()
{
    m_Center.set_x( m_Length() * m_Origin() );
}

//==== Scale ====//
void CylinderGeom::ApplyScale( double currentScale )
{
    m_Length *= currentScale;
    m_Diameter *= currentScale;
}
