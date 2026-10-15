//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

// STLOptionsScreen.cpp: implementation of the STLOptionsScreen class.
//
//////////////////////////////////////////////////////////////////////

#include "STLOptionsScreen.h"

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

STLOptionsScreen::STLOptionsScreen( ScreenMgr* mgr ) : BasicScreen( mgr, 320, 330, "STL Options" )
{
    m_FLTK_Window->callback( staticCloseCB, this );

    m_OkFlag = false;
    m_PrevMultiSolid = false;
    m_PrevPropExportOrigin = false;
    m_PrevResOverride = false;
    m_PrevTessU = m_PrevSectTessU = m_PrevTessW = m_PrevCapTess = 0;

    m_GenLayout.SetGroupAndScreen( m_FLTK_Window, this );
    m_GenLayout.AddY( 25 );

    m_GenLayout.AddYGap();

    m_GenLayout.AddButton( m_MultiSolidToggle, "Tagged Multi Solid File (Non-Standard)" );
    m_GenLayout.AddButton( m_PropExportOriginToggle, "Export Unintersected Props At Origin" );

    // TitanVSP: the export's own resolution, off unless asked for.
    m_GenLayout.AddYGap();
    m_GenLayout.AddDividerBox( "Resolution" );
    m_GenLayout.AddButton( m_ResOverrideToggle, "Modify Resolution" );
    m_GenLayout.SetButtonWidth( 110 );
    m_GenLayout.AddSlider( m_TessUSlider, "Tess U", 100, "%5.0f" );
    m_GenLayout.AddSlider( m_SectTessUSlider, "Section Tess U", 100, "%5.0f" );
    m_GenLayout.AddSlider( m_TessWSlider, "Tess W", 100, "%5.0f" );
    m_GenLayout.AddSlider( m_CapTessSlider, "Cap Tess", 20, "%5.0f" );

    m_GenLayout.AddY( 20 );
    m_GenLayout.SetFitWidthFlag( false );
    m_GenLayout.SetSameLineFlag( true );
    m_GenLayout.SetButtonWidth( 100 );

    m_GenLayout.AddX( 20 );
    m_GenLayout.AddButton( m_OkButton, "OK" );
    m_GenLayout.AddX( 10 );
    m_GenLayout.AddButton( m_CancelButton, "Cancel" );
    m_GenLayout.ForceNewLine();
}

STLOptionsScreen::~STLOptionsScreen()
{
}

bool STLOptionsScreen::Update()
{
    BasicScreen::Update();

    Vehicle *veh = VehicleMgr.GetVehicle();

    if( veh )
    {
        m_MultiSolidToggle.Update( veh->m_STLMultiSolid.GetID() );
        m_PropExportOriginToggle.Update( veh->m_STLExportPropMainSurf.GetID() );

        m_ResOverrideToggle.Update( veh->m_STLResOverride.GetID() );
        m_TessUSlider.Update( veh->m_STLTessU.GetID() );
        m_SectTessUSlider.Update( veh->m_STLSectTessU.GetID() );
        m_TessWSlider.Update( veh->m_STLTessW.GetID() );
        m_CapTessSlider.Update( veh->m_STLCapTess.GetID() );

        SliderAdjRangeInput* res[] = { &m_TessUSlider, &m_SectTessUSlider, &m_TessWSlider, &m_CapTessSlider };
        for ( SliderAdjRangeInput* r : res )
        {
            if ( veh->m_STLResOverride() )
            {
                r->Activate();
            }
            else
            {
                r->Deactivate();
            }
        }
    }

    m_FLTK_Window->redraw();

    return false;
}


void STLOptionsScreen::Show()
{
    m_ScreenMgr->SetUpdateFlag( true );
    BasicScreen::Show();
}

void STLOptionsScreen::CallBack( Fl_Widget* w )
{
    m_ScreenMgr->SetUpdateFlag( true );
}

void STLOptionsScreen::GuiDeviceCallBack( GuiDevice* device )
{
    assert( m_ScreenMgr );

    if ( device == &m_OkButton )
    {
        m_OkFlag = true;
        Hide();
    }
    else if ( device == &m_CancelButton )
    {
        RestorePrev();
        Hide();
    }

    m_ScreenMgr->SetUpdateFlag( true );
}

bool STLOptionsScreen::ShowSTLOptionsScreen()
{
    Show();

    m_OkFlag = false;

    Vehicle *veh = VehicleMgr.GetVehicle();

    if( veh )
    {
        SavePrev();
    }

    while( m_FLTK_Window->shown() )
    {
        Fl::wait();
    }

    return m_OkFlag;
}

void STLOptionsScreen::CloseCallBack( Fl_Widget *w )
{
    assert( m_ScreenMgr );

    RestorePrev();

    Hide();
}

// The settings as the screen opened, for Cancel and Close to go back to.
void STLOptionsScreen::SavePrev()
{
    Vehicle *veh = VehicleMgr.GetVehicle();

    if( veh )
    {
        m_PrevMultiSolid = veh->m_STLMultiSolid();
        m_PrevPropExportOrigin = veh->m_STLExportPropMainSurf();
        m_PrevResOverride = veh->m_STLResOverride();
        m_PrevTessU = veh->m_STLTessU();
        m_PrevSectTessU = veh->m_STLSectTessU();
        m_PrevTessW = veh->m_STLTessW();
        m_PrevCapTess = veh->m_STLCapTess();
    }
}

void STLOptionsScreen::RestorePrev()
{
    Vehicle *veh = VehicleMgr.GetVehicle();

    if( veh )
    {
        veh->m_STLMultiSolid.Set( m_PrevMultiSolid );
        veh->m_STLExportPropMainSurf.Set( m_PrevPropExportOrigin );
        veh->m_STLResOverride.Set( m_PrevResOverride );
        veh->m_STLTessU.Set( m_PrevTessU );
        veh->m_STLSectTessU.Set( m_PrevSectTessU );
        veh->m_STLTessW.Set( m_PrevTessW );
        veh->m_STLCapTess.Set( m_PrevCapTess );
    }
}
