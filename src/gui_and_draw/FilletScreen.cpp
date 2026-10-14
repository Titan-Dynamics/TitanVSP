//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//
//
//////////////////////////////////////////////////////////////////////

#include "FilletScreen.h"
#include "ScreenMgr.h"
#include "FilletGeom.h"


//==== Constructor ====//
FilletScreen::FilletScreen( ScreenMgr* mgr ) : GeomScreen( mgr, 400, 800, "Fillet" )
{
    Fl_Group* design_tab = AddTab( "Design" );
    Fl_Group* design_group = AddSubGroup( design_tab, 5 );

    m_DesignLayout.SetGroupAndScreen( design_group, this );
    m_DesignLayout.AddDividerBox( "Fillet" );
    m_DesignLayout.AddOutput( m_StatusOutput, 2 );

    m_DesignLayout.AddYGap();
    m_DesignLayout.AddDividerBox( "Design" );
    m_DesignLayout.AddSlider( m_RadiusSlider, "Radius", 10, "%7.3f" );
    m_DesignLayout.AddSlider( m_NumStationsSlider, "Stations", 100, "%5.0f" );
    m_DesignLayout.AddSlider( m_TETaperSlider, "TE Taper", 1, "%5.3f" );

    m_DesignLayout.AddYGap();
}


//==== Show Fillet Screen ====//
void FilletScreen::Show()
{
    if ( Update() )
    {
        GeomScreen::Show();
    }
}

//==== Update Fillet Screen ====//
bool FilletScreen::Update()
{
    assert( m_ScreenMgr );

    Geom* geom_ptr = m_ScreenMgr->GetCurrGeom();
    if ( !geom_ptr || geom_ptr->GetType().m_Type != FILLET_GEOM_TYPE )
    {
        Hide();
        return false;
    }

    GeomScreen::Update();

    FilletGeom* fillet_ptr = dynamic_cast< FilletGeom* >( geom_ptr );
    assert( fillet_ptr );

    m_StatusOutput.Update( fillet_ptr->GetStatus() );
    m_RadiusSlider.Update( fillet_ptr->m_Radius.GetID() );
    m_NumStationsSlider.Update( fillet_ptr->m_NumStations.GetID() );
    m_TETaperSlider.Update( fillet_ptr->m_TETaper.GetID() );

    return true;
}


//==== Non Menu Callbacks ====//
void FilletScreen::CallBack( Fl_Widget *w )
{
    GeomScreen::CallBack( w );
}
