//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

// CylinderScreen.cpp: the Cylinder's window -- its length and diameter (TitanVSP).
//
//////////////////////////////////////////////////////////////////////

#include "CylinderScreen.h"
#include "ScreenMgr.h"
#include "CylinderGeom.h"

//==== Constructor ====//
CylinderScreen::CylinderScreen( ScreenMgr* mgr ) : GeomScreen( mgr, 400, 800, "Cylinder" )
{
    Fl_Group* design_tab = AddTab( "Design" );
    Fl_Group* design_group = AddSubGroup( design_tab, 5 );

    m_DesignLayout.SetGroupAndScreen( design_group, this );
    m_DesignLayout.AddDividerBox( "Design" );

    m_DesignLayout.AddSlider( m_LengthSlider, "Length", 10, "%7.3f" );
    m_DesignLayout.AddSlider( m_DiameterSlider, "Diameter", 10, "%7.3f" );
    m_DesignLayout.AddYGap();
}

//==== Show Cylinder Screen ====//
void CylinderScreen::Show()
{
    if ( Update() )
    {
        GeomScreen::Show();
    }
}

//==== Update Cylinder Screen ====//
bool CylinderScreen::Update()
{
    assert( m_ScreenMgr );

    Geom* geom_ptr = m_ScreenMgr->GetCurrGeom();
    if ( !geom_ptr || geom_ptr->GetType().m_Type != CYLINDER_GEOM_TYPE )
    {
        Hide();
        return false;
    }

    GeomScreen::Update();

    CylinderGeom* cyl_ptr = dynamic_cast< CylinderGeom* >( geom_ptr );
    assert( cyl_ptr );
    m_LengthSlider.Update( cyl_ptr->m_Length.GetID() );
    m_DiameterSlider.Update( cyl_ptr->m_Diameter.GetID() );

    return true;
}

//==== Non Menu Callbacks ====//
void CylinderScreen::CallBack( Fl_Widget *w )
{
    GeomScreen::CallBack( w );
}
