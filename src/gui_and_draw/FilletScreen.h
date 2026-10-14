//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

// FilletScreen.h: UI for Fillet Geom
//
//////////////////////////////////////////////////////////////////////

#if !defined(FILLET_SCREEN__INCLUDED_)
#define FILLET_SCREEN__INCLUDED_

#include "ScreenBase.h"
#include "GuiDevice.h"

#include <FL/Fl.H>

class FilletScreen : public GeomScreen
{
public:
    FilletScreen( ScreenMgr* mgr );
    virtual ~FilletScreen()                            {}

    virtual void Show();
    virtual bool Update();

    virtual void CallBack( Fl_Widget *w );

protected:

    GroupLayout m_DesignLayout;

    StringOutput m_StatusOutput;
    SliderAdjRangeInput m_RadiusSlider;
    SliderAdjRangeInput m_NumStationsSlider;
    SliderAdjRangeInput m_TETaperSlider;
};

#endif // !defined(FILLET_SCREEN__INCLUDED_)
