//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

//
// Programmatic main GUI window
// Rob McDonald
//
//////////////////////////////////////////////////////////////////////

#if !defined(MAINSCREEN__INCLUDED_)
#define MAINSCREEN__INCLUDED_

#include "MenuItem.h"
#include "ScreenBase.h"
#include "GuiDevice.h"
#include "FL/Fl_Box.H"

namespace VSPGUI
{
class VspGlWindow;
}

class SelectFileScreen;

#define GL_WINDOW_DX 0
#define GL_WINDOW_DY 25

class MainVSPScreen : public ActionScreen
{
public:
    MainVSPScreen( ScreenMgr* mgr );
    virtual ~MainVSPScreen();

    void Show();
    void Hide();
    bool Update();

    virtual void CallBack( Fl_Widget *w );
    virtual void CloseCallBack( Fl_Widget *w );
    virtual void GuiDeviceCallBack( GuiDevice* device );

    void ResizeWindow( int pixel_w, int pixel_h );

    VSPGUI::VspGlWindow * GetGLWindow()
    {
        return m_GlWin;
    }

    static int ConvertViewEnums( int v );
    void SetAllViews( int view );
    void SetView( int viewport, int view );
    void SetView( int view );
    void FitView( bool all );
    bool GetFitBndBox( BndBox & bbox );     // TitanVSP: what Fit On Screen frames.
    void FitViewAnimated( bool animate );   // TitanVSP: F, and on opening a file.
    void AlignView( const vec3d & v );
    void AlignViewToGeom( string geom_id );
    void ResetViews();
    void SetWindowLayout( int r, int c );

    void ShowReturnToAPIImplementation();
    void HideReturnToAPIImplementation();
    void ShowExitImplementation();
    void HideExitImplementation();

    void ScreenGrab( const string & fname, int w, int h, bool transparentBG, bool autocrop );
    void SetViewAxis( bool vaxis );
    void SetPerspective( bool persp );

    // TitanVSP: Blender's auto perspective.  Snapping to Top, Front, Left, Bottom, Rear or Right
    // shows the view orthographic; orbiting it afterwards, or picking an isometric view, goes back
    // to perspective if that's what it was.  The model's own Perspective setting isn't touched.
    void EndSnapOrtho();
    void TogglePerspective();   // View -> Perspective (5): a snapped view goes back to perspective.
    void ApplyPerspective();
    void SetShowBorders( bool brdr );
    void SetBackground( double r, double g, double b );

    void EnableStopGUIMenuItem();
    void DisableStopGUIMenuItem();
    void EnableExitMenuItem();
    void DisableExitMenuItem();

protected:

    virtual void ActionCB( void * data );

    void BoldEntries( const string &mpath );

    void SetFileLabel( const string &fname );

    // TitanVSP: File -> Open Recent.  The last models opened or saved, newest first, kept in the
    // preferences store so they last between sessions.
    void OpenVSPFile( const string & fname );
    void LoadRecentFiles();
    void SaveRecentFiles();
    void AddRecentFile( const string & fname );
    void RebuildRecentMenu();
    static void RecentCB( Fl_Widget * w, void * data );
    static void ClearRecentCB( Fl_Widget * w, void * data );
    vector< string > m_RecentFiles;
    string m_RecentSeen;        // The model's file name last time Update looked.
    struct RecentItem
    {
        MainVSPScreen * m_Screen;
        int m_Index;
    };
    RecentItem m_RecentItems[10];

    virtual void NewModel();
    virtual void ExitVSP();

    ActionMenuItem m_FileMenuItem;

    ActionMenuItem m_NewMenuItem;
    ActionMenuItem m_OpenMenuItem;
    ActionMenuItem m_SaveMenuItem;
    ActionMenuItem m_SaveAsMenuItem;
    ActionMenuItem m_SaveSetMenuItem;
    ActionMenuItem m_InsertMenuItem;
    ScreenMenuItem m_ImportMenuItem;
    ScreenMenuItem m_ExportMenuItem;
    // ActionMenuItem m_SetTempDirMenuItem;
    ActionMenuItem m_RunScriptMenuItem;
    ScreenMenuItem m_PreferencesMenuItem;
    ActionMenuItem m_ExitMenuItem;
    ActionMenuItem m_StopGUIMenuItem;

    ActionMenuItem m_UndoMenuItem;
    ActionMenuItem m_CutMenuItem;
    ActionMenuItem m_CopyMenuItem;
    ActionMenuItem m_PasteMenuItem;
    ActionMenuItem m_DeleteMenuItem;
    ActionMenuItem m_SelAllMenuItem;
    ActionMenuItem m_PickMenuItem;

    ActionMenuItem m_OneMenuItem;
    ActionMenuItem m_FourMenuItem;
    ActionMenuItem m_TwoHMenuItem;
    ActionMenuItem m_TwoVMenuItem;
    ScreenMenuItem m_BackgroundMenuItem;
    ScreenMenuItem m_Background3DMenuItem;
    ScreenMenuItem m_ScreenshotMenuItem;
    VSPCheckMenuItem m_AxisMenuItem;
    VSPCheckMenuItem m_BorderMenuItem;
    VSPCheckMenuItem m_PerspectiveMenuItem;
    VSPCheckMenuItem m_GridMenuItem;
    VSPCheckMenuItem m_AntialiasMenuItem;
    VSPCheckMenuItem m_OutlinesMenuItem;

    ActionMenuItem m_TopMenuItem;
    ActionMenuItem m_FrontMenuItem;
    ActionMenuItem m_LeftSideMenuItem;
    ActionMenuItem m_LeftIsoMenuItem;
    ActionMenuItem m_BottomMenuItem;
    ActionMenuItem m_RearMenuItem;
    ActionMenuItem m_RightSideMenuItem;
    ActionMenuItem m_RightIsoMenuItem;
    ActionMenuItem m_CenterMenuItem;
    ActionMenuItem m_CenterAllMenuItem;
    ScreenMenuItem m_AdjustMenuItem;
    ActionMenuItem m_SetCORMenuItem;
    ActionMenuItem m_SetVNMenuItem;
    ActionMenuItem m_AlignGeomMenuItem;
    ActionMenuItem m_FitViewMenuItem;
    ActionMenuItem m_FitAllViewMenuItem;
    ActionMenuItem m_ResetAllViewMenuItem;
    // ActionMenuItem m_AntialiasMenuItem;

    ScreenMenuItem m_GeomMenuItem;
    ScreenMenuItem m_SetEditMenuItem;
    ScreenMenuItem m_VarPresetMenuItem;
    ScreenMenuItem m_ModeEditMenuItem;
    ScreenMenuItem m_LinkMenuItem;
    ScreenMenuItem m_DesVarMenuItem;
    ScreenMenuItem m_LabelMenuItem;
    ScreenMenuItem m_LightMenuItem;
    ScreenMenuItem m_ClipMenuItem;
    ScreenMenuItem m_TextureMenuItem;
    ScreenMenuItem m_AdvLinkMenuItem;
    ScreenMenuItem m_UserParmMenuItem;
    ScreenMenuItem m_FitModelMenuItem;
    ScreenMenuItem m_SnapToMenuItem;
    ScreenMenuItem m_AttributeMenuItem;
    ScreenMenuItem m_VehNotesMenuItem;

    ScreenMenuItem m_CompGeomMenuItem;
    ScreenMenuItem m_WaveDragMenuItem;
    ScreenMenuItem m_PSliceMenuItem;
    ScreenMenuItem m_MassPropMenuItem;
    ScreenMenuItem m_ProjectionMenuItem;
    ScreenMenuItem m_InterferenceMenuItem;
    ScreenMenuItem m_SurfIntMenuItem;
    ScreenMenuItem m_CFDMeshMenuItem;
    ScreenMenuItem m_StructMenuItem;
    ScreenMenuItem m_StructAssemblyMenuItem;
    ScreenMenuItem m_DegenGeomMenuItem;
    ScreenMenuItem m_VSPAEROMenuItem;
    ScreenMenuItem m_ParasiteDragMenuItem;
    ScreenMenuItem m_AeroStructMenuItem;

    // ScreenMenuItem m_AboutMenuItem;
    ActionMenuItem m_OnlineHelpMenuItem;
    ActionMenuItem m_TestParmMapMenuItem;
    ActionMenuItem m_TestParmContainerMapMenuItem;
    // ScreenMenuItem m_VersionMenuItem;


    GroupLayout m_GenLayout;

    Fl_Sys_Menu_Bar *m_MenuBar;
    VSPGUI::VspGlWindow * m_GlWin;
    Fl_Box * m_TitleBox;
    Fl_Box * m_FileNameBox;

    bool m_ShowXYZArrow;
    bool m_ShowBorder;
    bool m_Perspective;
    bool m_SnapOrtho = false;
    bool m_FitPending = true;   // Fit the model once the view has a size: at launch, and on opening a file.
    bool m_FitScheduled = false;
    static void FitPendingCB( void * data );   // Showing an axis view orthographic, in place of perspective.
};


#endif // !defined(MAINSCREEN__INCLUDED_)
