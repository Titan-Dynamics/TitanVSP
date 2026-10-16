//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

// CylinderScreen.h: the Cylinder's window -- its length and diameter (TitanVSP).
//
//////////////////////////////////////////////////////////////////////

#if !defined(CYLINDERSCREEN__INCLUDED_)
#define CYLINDERSCREEN__INCLUDED_

#include "ScreenBase.h"
#include "GuiDevice.h"

#include <FL/Fl.H>

class CylinderScreen : public GeomScreen
{
public:
    CylinderScreen( ScreenMgr* mgr );
    virtual ~CylinderScreen()                       {}

    virtual void Show();
    virtual bool Update();

    virtual void CallBack( Fl_Widget *w );

protected:

    GroupLayout m_DesignLayout;
    SliderAdjRangeInput m_LengthSlider;
    SliderAdjRangeInput m_DiameterSlider;
};

#endif // !defined(CYLINDERSCREEN__INCLUDED_)
