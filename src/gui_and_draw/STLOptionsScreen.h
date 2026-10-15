//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

// STLOptionsScreen.h: interface for controlling STL export options.
//
//////////////////////////////////////////////////////////////////////

#ifndef STLOPTIONSSCREEN_H
#define STLOPTIONSSCREEN_H

#include "ScreenMgr.h"
#include "ScreenBase.h"
#include "Vehicle.h"
#include "GuiDevice.h"

class STLOptionsScreen : public BasicScreen
{
public:
    STLOptionsScreen( ScreenMgr* mgr );
    virtual ~STLOptionsScreen();

    void Show();
    bool Update();

    void CallBack( Fl_Widget *w );
    static void staticScreenCB( Fl_Widget *w, void* data )
    {
        ( ( STLOptionsScreen* )data )->CallBack( w );
    }
    virtual void CloseCallBack( Fl_Widget *w );
    virtual void GuiDeviceCallBack( GuiDevice* device );

    bool ShowSTLOptionsScreen();

protected:

    GroupLayout m_GenLayout;

    ToggleButton m_MultiSolidToggle;
    ToggleButton m_PropExportOriginToggle;

    bool m_PrevMultiSolid;
    bool m_PrevPropExportOrigin;

    // TitanVSP: the export's own resolution.
    ToggleButton m_ResOverrideToggle;
    SliderAdjRangeInput m_TessUSlider;
    SliderAdjRangeInput m_SectTessUSlider;
    SliderAdjRangeInput m_TessWSlider;
    SliderAdjRangeInput m_CapTessSlider;

    bool m_PrevResOverride;
    int m_PrevTessU, m_PrevSectTessU, m_PrevTessW, m_PrevCapTess;

    void SavePrev();
    void RestorePrev();

    TriggerButton m_OkButton;
    TriggerButton m_CancelButton;

    bool m_OkFlag;
};

#endif  // STLOPTIONSSCREEN_H
