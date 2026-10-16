//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

#ifdef WIN32
#undef _HAS_STD_BYTE
#define _HAS_STD_BYTE 0
#endif

#include <cmath>

#include "MainGLWindow.h"

#include "Background3DMgr.h"
#include "BORGeom.h"
#include "Camera.h"
#include "CfdMeshScreen.h"
#include "Clipping.h"
#include "ClippingScreen.h"
#include "CurveEditScreen.h"
#include "Display.h"
#include "Entity.h"
#include "FitModelScreen.h"
#include "GeometryAnalysisScreen.h"
#include "GraphicEngine.h"
#include "GraphicSingletons.h"
#include "Image.h"
#include "LayoutMgr.h"
#include "Lighting.h"
#include "ManageBackground3DScreen.h"
#include "ManageCORScreen.h"
#include "ManageGeomScreen.h"
#include "CloneDeleteDialog.h"
#include "ManageLightingScreen.h"
#include "ManageMeasureScreen.h"
#include "ManageViewScreen.h"
#include "MassPropScreen.h"
#include "MeasureMgr.h"
#include "Pickable.h"
#include "PickablePnts.h"
#include "Probe.h"
#include "Protractor.h"
#include "Routing.h"
#include "Renderable.h"
#include "RoutingScreen.h"
#include "Ruler.h"
#include "Scene.h"
#include "ScreenMgr.h"
#include "Selectable.h"
#include "SelectedLoc.h"
#include "SelectedPnt.h"
#include "StructAssemblyScreen.h"
#include "StructScreen.h"
#include "SurfaceIntersectionScreen.h"
#include "Texture2D.h"
#include "Vehicle.h"
#include "VehicleMgr.h"
#include "Viewport.h"
#include "Watermark.h"
#include "VSPAEROScreen.h"
#include "WaveDragScreen.h"

#include <glm/gtc/matrix_transform.hpp>
#include <FL/fl_ask.H>
#include <FL/gl.h>      // gl_draw, for the move/rotate prompt.  After GLEW.
#include <FL/platform.H>

#pragma warning(disable:4244)

using namespace VSPGraphic;

namespace VSPGUI
{
VspGlWindow::VspGlWindow( int x, int y, int w, int h, ScreenMgr * mgr, DrawObj::ScreenEnum drawObjScreen )
    : Fl_Gl_Window( x, y, w, h, "VSP GL Window" )
{
    mode( FL_RGB | FL_ALPHA | FL_DEPTH | FL_DOUBLE | FL_MULTISAMPLE );
    m_GEngine = new VSPGraphic::GraphicEngine();

    m_ScreenMgr = mgr;

    // Link this GUI to one of drawObj screen.
    m_LinkedScreen = drawObjScreen;

    m_GizmoAxis = -1;
    m_GizmoHover = -1;

    m_ClickCandidate = false;
    m_ClickX = m_ClickY = 0;

    m_XfMode = XF_NONE;
    m_XfAxis = -1;
    m_XfSwallowKeyUp = false;

    // Blanks are drawn in each viewport right after its scene, so parts in front hide them.
    if ( m_LinkedScreen == DrawObj::VSP_MAIN_SCREEN )
    {
        m_GEngine->getDisplay()->setSceneOverlay( [this]( VSPGraphic::Viewport * vp )
        {
            GridDraw( vp );
            BlankDraw( vp );
        } );
    }
    m_GizmoPx = 1.0f;
    m_GizmoSteps = 0;
    m_GizmoUndoSaved = false;

    // One Screen
    m_GEngine->getDisplay()->setDisplayLayout( 1, 1 );
    m_GEngine->getDisplay()->selectViewport( 0 );

    m_mouse_x = m_mouse_y = 0xFFFFFFF;

    m_initialized = false;

    m_prevViewport = -1;
    m_prevLB = m_prevRB = m_prevMB = glm::vec2( fNAN );
    m_prevAltLB = m_prevCtrlLB = m_prevMetaLB = m_prevZLB = glm::vec2( fNAN );
    m_prevLBRB = glm::vec2( fNAN );

    m_ScreenMgr = mgr;

    m_hasSetSize = false;
    m_noRotate = false;

#ifdef __APPLE__
#if FL_API_VERSION >= 10304
    Fl::use_high_res_GL( true );
#endif
#endif
}
VspGlWindow::~VspGlWindow()
{
    delete m_GEngine;
}

void VspGlWindow::setWindowLayout( int row, int column )
{
    m_GEngine->getDisplay()->setDisplayLayout( row, column );
    m_GEngine->getDisplay()->resize( pixel_w(), pixel_h() );
    m_GEngine->getDisplay()->selectViewport( 0 );
}

void VspGlWindow::setView( VSPGraphic::Common::VSPenum type )
{
    m_GEngine->getDisplay()->changeView( type );
}

void VspGlWindow::relativePan( float x, float y )
{
    m_GEngine->getDisplay()->relativePan( x, y );
}

void VspGlWindow::setCOR( glm::vec3 center )
{
    m_GEngine->getDisplay()->setCOR( -center.x, -center.y, -center.z );
    m_GEngine->getDisplay()->center();
}

glm::vec3 VspGlWindow::getCOR()
{
    return m_GEngine->getDisplay()->getCOR() * -1.0f;
}

glm::vec2 VspGlWindow::getPanValues()
{
    return m_GEngine->getDisplay()->getPanValues();
}

void VspGlWindow::relativeZoom( float zoomValue )
{
    m_GEngine->getDisplay()->relativeZoom( zoomValue );
}

float VspGlWindow::getRelativeZoomValue()
{
    return m_GEngine->getDisplay()->getRelativeZoomValue();
}

void VspGlWindow::rotateSphere( float angleX, float angleY, float angleZ )
{
    m_GEngine->getDisplay()->rotateSphere( angleX, angleY, angleZ );
}

glm::vec3 VspGlWindow::getRotationEulerAngles()
{
    return m_GEngine->getDisplay()->getRotationEulerAngles();
}

void VspGlWindow::draw()
{
    // Initialize Glew when context is created.
    _initGLEW();

    if ( !valid() )
    {
        //===== Quick hack to set default opengl screen size (only run once) =====//
        if ( !m_hasSetSize )
        {
            m_GEngine->getDisplay()->setDefaultScreenSize( pixel_w(), pixel_h() );
            m_hasSetSize = true;
        }

        m_GEngine->getDisplay()->resize( pixel_w(), pixel_h() );
        UpdateViewportParms();

        //Make sure the current width and height update
        m_ScreenMgr->GetScreen( vsp::VSP_SCREENSHOT_SCREEN )->Update();
    }

    m_GEngine->draw( m_mouse_x, m_mouse_y );

    GizmoDraw();
    XfDraw();
    KeysDraw();
}

int VspGlWindow::handle( int fl_event )
{
    // Move origin from upper left corner to lower left corner.
    int x = Fl::event_x();
    int y = h() - Fl::event_y();

    x *= pixels_per_unit();
    y *= pixels_per_unit();

    m_mouse_x = x;
    m_mouse_y = y;

    int dx = Fl::event_dx();
    int dy = Fl::event_dy();

    // TitanVSP: taking hold of the view with the mouse stops a view change where it is.
    if ( fl_event == FL_PUSH || fl_event == FL_MOUSEWHEEL )
    {
        StopViewAnim();
    }

    switch( fl_event )
    {
    case FL_ENTER:
        cursor( FL_CURSOR_CROSS );
        return 1;

    case FL_LEAVE:
        cursor( FL_CURSOR_DEFAULT );
        return 1;

    case FL_PUSH:
        m_ClickCandidate = false;
        m_HoverPos = glm::vec2( x, y );
        // The shortcuts button, and a click anywhere else while its list is open closes it.
        if ( KeysIconHit( x, y ) )
        {
            m_KeysOpen = !m_KeysOpen;
            redraw();
            return 1;
        }
        if ( m_KeysOpen )
        {
            m_KeysOpen = false;
            redraw();
            if ( KeysPanelHit( x, y ) )
            {
                return 1;
            }
        }
        if ( m_XfMode != XF_NONE )
        {
            // While typing a move or rotate: left click applies it, any other button cancels.
            XfEnd( Fl::event_button() == FL_LEFT_MOUSE );
            return 1;
        }
        if ( GizmoPush( x, y ) )
        {
            return 1;
        }
        // Ctrl as well as Shift adds or takes away, as in Blender; a Ctrl drag still zooms.
        m_ClickCandidate = m_LinkedScreen == DrawObj::VSP_MAIN_SCREEN && Fl::event_button() == FL_LEFT_MOUSE &&
                           !Fl::event_state( FL_ALT | FL_META ) && !m_GEngine->getScene()->isPickingEnabled();
        m_ClickX = x;
        m_ClickY = y;
        OnPush( x, y );
        return 1;

    case FL_DRAG:
        if ( m_GizmoAxis >= 0 )
        {
            GizmoDrag( x, y );
            return 1;
        }
        if ( m_ClickCandidate && std::abs( x - m_ClickX ) + std::abs( y - m_ClickY ) > 5 * pixels_per_unit() )
        {
            m_ClickCandidate = false;
        }
        OnDrag( x, y );
        return 1;

    case FL_RELEASE:
        if ( m_GizmoAxis >= 0 )
        {
            GizmoRelease();
            return 1;
        }
        OnRelease( x, y );
        if ( m_ClickCandidate && Fl::event_button() == FL_LEFT_MOUSE )
        {
            m_ClickCandidate = false;
            ClickSelect( m_ClickX, m_ClickY, Fl::event_state( FL_SHIFT | FL_CTRL ) != 0 );
        }
        return 1;

    case FL_MOUSEWHEEL:
        OnWheelScroll( dx, dy, x, y );
        return 1;

    case FL_MOVE:
        m_HoverPos = glm::vec2( x, y );
        if ( m_XfMode != XF_NONE )
        {
            XfMouse( x, y );
            return 1;
        }
        if ( m_LinkedScreen == DrawObj::VSP_MAIN_SCREEN && KeysIconHit( x, y ) != m_KeysHover )
        {
            m_KeysHover = !m_KeysHover;
            redraw();
        }
        if( m_GEngine->getScene()->isPickingEnabled() )
        {
            redraw();
        }
        else
        {
            int hover = GizmoShown() ? GizmoHit( x, y ) : -1;
            if ( hover != m_GizmoHover )
            {
                m_GizmoHover = hover;
                redraw();
            }
        }
        return 1;

    case FL_KEYDOWN:
        if ( XfKey() || ViewKey() || BlenderKey() )
        {
            return 1;
        }
        return OnKeydown();

    case FL_SHORTCUT:
        // G or R with the mouse over the view, even when another widget has the keyboard.
        if ( ( m_XfMode != XF_NONE || Fl::event_inside( this ) ) && XfKey() )
        {
            return 1;
        }
        if ( Fl::event_inside( this ) && ( ViewKey() || BlenderKey() ) )
        {
            return 1;
        }
        break;

    case FL_KEYUP:
        if ( Fl::event_key() == 'z' )
        {
            m_ZHeld = false;
        }
        if ( m_XfMode != XF_NONE && ( Fl::event_key() == FL_Control_L || Fl::event_key() == FL_Control_R ) )
        {
            m_XfSnap = false;
            XfApply( false );
            redraw();
        }
        if ( m_XfMode != XF_NONE || m_XfSwallowKeyUp )
        {
            // The keys belong to the move or rotate, including the Esc that cancelled it, which
            // would otherwise clear the selection.
            if ( m_XfMode == XF_NONE )
            {
                m_XfSwallowKeyUp = false;
            }
            return 1;
        }
        return OnKeyup( x, y );

    case FL_FOCUS:
        return 1;

    case FL_UNFOCUS:
        return 1;
    }
    return Fl_Gl_Window::handle( fl_event );
}

void VspGlWindow::update()
{
    Vehicle* vPtr = VehicleMgr.GetVehicle();

    if ( vPtr && m_initialized )
    {
        make_current();

        VSPGraphic::Display * display = m_GEngine->getDisplay();
        if ( display )
        {
            VSPGraphic::Camera * camera = display->getCamera();
            if ( camera )
            {
                float zn, zf;

                vPtr->UpdateBBox();
                BndBox bb = vPtr->GetBndBox();

                float d = std::max( 10.0, bb.GetLargestDist() );

                vec3d cen = bb.GetCenter();

                zf = 2.0f * ( std::abs( cen[ cen.major_comp() ] ) + d );

                zf = std::max( zf, 2500.0f );

                zn = -zf;

                camera->setZNearFar( zn, zf );
            }

            if ( AttributeMgr.GetDirtyFlag( vsp::ATTR_GROUP_WATERMARK ) )
            {
                AttributeMgr.ClearDirtyFlag( vsp::ATTR_GROUP_WATERMARK );

                VSPGraphic::Viewport *vp = display->getViewport();

                if ( vp )
                {
                    VSPGraphic::Watermark * wm = vp->getWatermark();

                    if ( wm )
                    {
                        if ( AttributeMgr.GetAttributePtr( ATTR_WM_SHOW ) )
                        {
                            display->getViewport()->showWatermark( AttributeMgr.GetAttributePtr( ATTR_WM_SHOW )->GetBool( 0 ) );
                            if ( AttributeMgr.GetAttributePtr( ATTR_WM_TEXT ) )
                            {
                                wm->setText( AttributeMgr.GetAttributePtr( ATTR_WM_TEXT )->GetString( 0 ) );
                            }

                            if ( AttributeMgr.GetAttributePtr( ATTR_WM_TEXTSCALE ) )
                            {
                                wm->setTextScale( AttributeMgr.GetAttributePtr( ATTR_WM_TEXTSCALE )->GetDouble( 0 ) );
                            }

                            vec3d tc = vec3d(0.,0.,0.);
                            vec3d bc = vec3d(0.,0.,0.);
                            vec3d fc = vec3d(0.,0.,0.);
                            double ta = 0.;
                            double ba = 0.;
                            double fa = 0.;

                            if ( AttributeMgr.GetAttributePtr( ATTR_WM_TEXTCOLOR ) )
                            {
                                tc = AttributeMgr.GetAttributePtr( ATTR_WM_TEXTCOLOR )->GetVec3d( 0 );
                            }
                            if ( AttributeMgr.GetAttributePtr( ATTR_WM_TEXTALPHA ) )
                            {
                                ta = AttributeMgr.GetAttributePtr( ATTR_WM_TEXTALPHA )->GetDouble( 0 );
                            }
                            if ( AttributeMgr.GetAttributePtr( ATTR_WM_EDGECOLOR ) )
                            {
                                bc = AttributeMgr.GetAttributePtr( ATTR_WM_EDGECOLOR )->GetVec3d( 0 );
                            }
                            if ( AttributeMgr.GetAttributePtr( ATTR_WM_EDGEALPHA ) )
                            {
                                ba = AttributeMgr.GetAttributePtr( ATTR_WM_EDGEALPHA )->GetDouble( 0 );
                            }
                            if ( AttributeMgr.GetAttributePtr( ATTR_WM_FILLCOLOR ) )
                            {
                                fc = AttributeMgr.GetAttributePtr( ATTR_WM_FILLCOLOR )->GetVec3d( 0 );
                            }
                            if ( AttributeMgr.GetAttributePtr( ATTR_WM_FILLALPHA ) )
                            {
                                fa = AttributeMgr.GetAttributePtr( ATTR_WM_FILLALPHA )->GetDouble( 0 );
                            }

                            wm->setTextColor( tc.x(), tc.y(), tc.z(), ta );
                            wm->setLineColor( bc.x(), bc.y(), bc.z(), ba );
                            wm->setFillColor( fc.x(), fc.y(), fc.z(), fa );

                        }
                        else
                        {
                            display->getViewport()->showWatermark( false );
                        }
                    }
                }
            }
        }

        vector<DrawObj *> drawObjs;
        LoadAllDrawObjs( drawObjs );

        // Load Objects to Renderer.
        _update( drawObjs );

        for( int i = 0; i < (int)drawObjs.size(); i++ )
        {
            if( drawObjs[i]->m_Screen == m_LinkedScreen )
            {
                drawObjs[i]->m_GeomChanged = false;
            }
        }
    }
}

void VspGlWindow::LoadAllDrawObjs( vector< DrawObj* > & drawObjs )
{
    Vehicle* vPtr = VehicleMgr.GetVehicle();

    if ( vPtr )
    {
        // Get Render Objects from Vehicle.
        vPtr->LoadDrawObjs( drawObjs );

        // Load Render Objects from fitModelScreen.
        FitModelScreen * fitModelScreen = dynamic_cast< FitModelScreen* >
            ( m_ScreenMgr->GetScreen( vsp::VSP_FIT_MODEL_SCREEN ) );
        if ( fitModelScreen )
        {
            fitModelScreen->LoadDrawObjs( drawObjs );
        }

        // Load Render Objects from massPropScreen.
        MassPropScreen* massPropScreen = dynamic_cast<MassPropScreen*>
            ( m_ScreenMgr->GetScreen( vsp::VSP_MASS_PROP_SCREEN ) );
        if ( massPropScreen )
        {
            massPropScreen->LoadDrawObjs( drawObjs );
        }

        // Load Render Objects from CfdMeshScreen.
        CfdMeshScreen * cfdScreen = dynamic_cast< CfdMeshScreen* >
            ( m_ScreenMgr->GetScreen( vsp::VSP_CFD_MESH_SCREEN ) );
        if( cfdScreen )
        {
            cfdScreen->LoadDrawObjs( drawObjs );
        }

        // Load Render Objects from SurfaceIntersectionScreen.
        SurfaceIntersectionScreen * surfScreen = dynamic_cast< SurfaceIntersectionScreen* >
            ( m_ScreenMgr->GetScreen( vsp::VSP_SURFACE_INTERSECTION_SCREEN ) );
        if( surfScreen )
        {
            surfScreen->LoadDrawObjs( drawObjs );
        }

        // I don't like having this Update() here.  However, there does not appear
        // to be a better place to put it.  Vehicle::Update does not get called
        // frequently enough.
        MeasureMgr.Update();
        // Load Render Objects from measureScreen.
        ManageMeasureScreen * measureScreen = dynamic_cast< ManageMeasureScreen* >
            ( m_ScreenMgr->GetScreen( vsp::VSP_MEASURE_SCREEN ) );
        if( measureScreen )
        {
            measureScreen->LoadDrawObjs( drawObjs );
        }

        // Needed here because Background position can depend on bounding box size.
        Background3DMgr.Update();
        // Load Render Objects from ManageBackground3DScreen.
        ManageBackground3DScreen * bg3dScreen = dynamic_cast< ManageBackground3DScreen* >
        ( m_ScreenMgr->GetScreen( vsp::VSP_BACKGROUND3D_SCREEN ) );
        if( bg3dScreen )
        {
            bg3dScreen->LoadDrawObjs( drawObjs );
        }

        // Load Render Objects from FeaStructScreen.
        StructScreen * structScreen = dynamic_cast< StructScreen* >
            ( m_ScreenMgr->GetScreen( vsp::VSP_STRUCT_SCREEN ) );
        if( structScreen )
        {
            structScreen->LoadDrawObjs( drawObjs );
        }

        // Load Render Objects from FeaStructAssemblyScreen.
        StructAssemblyScreen * structAssemblyScreen = dynamic_cast< StructAssemblyScreen* >
            ( m_ScreenMgr->GetScreen( vsp::VSP_STRUCT_ASSEMBLY_SCREEN ) );
        if( structAssemblyScreen )
        {
            structAssemblyScreen->LoadDrawObjs( drawObjs );
        }

        // Load Render Objects from lightScreen.
        ManageLightingScreen * lightScreen = dynamic_cast< ManageLightingScreen* >
            ( m_ScreenMgr->GetScreen( vsp::VSP_LIGHTING_SCREEN ) );
        if( lightScreen )
        {
            lightScreen->LoadDrawObjs( drawObjs );
        }

        // Load Render Objects from geomScreen.
        ManageGeomScreen * geomScreen = dynamic_cast< ManageGeomScreen* >
            ( m_ScreenMgr->GetScreen( vsp::VSP_MANAGE_GEOM_SCREEN ) );
        if( geomScreen )
        {
            geomScreen->LoadDrawObjs( drawObjs );
        }

        // Load Render Objects for routing screen
        if ( geomScreen )
        {
            RoutingScreen * routingScreen = dynamic_cast<RoutingScreen*>
                ( geomScreen->GetGeomScreen( vsp::ROUTING_GEOM_SCREEN ) );
            if ( routingScreen )
            {
                routingScreen->LoadDrawObjs( drawObjs );
            }
        }

        // Load Render Objects from corScreen ( Center of Rotation ).
        ManageCORScreen * corScreen = dynamic_cast< ManageCORScreen* >
            ( m_ScreenMgr->GetScreen( vsp::VSP_COR_SCREEN ) );
        if( corScreen )
        {
            corScreen->LoadDrawObjs( drawObjs );
        }

        // Load Render Objects from clipScreen ( Clipping ).
        ClippingScreen * clipScreen = dynamic_cast< ClippingScreen* >
            ( m_ScreenMgr->GetScreen( vsp::VSP_CLIPPING_SCREEN ) );
        if( clipScreen )
        {
            clipScreen->LoadDrawObjs( drawObjs );
        }

        // Load Render Objects from WDSrceen ( Wave Drag Tool)
        WaveDragScreen * WDScreen = dynamic_cast< WaveDragScreen* >
            ( m_ScreenMgr->GetScreen( vsp::VSP_WAVEDRAG_SCREEN ) );
        if( WDScreen )
        {
            WDScreen->LoadDrawObjs( drawObjs );
        }

        // Load Render Objects for VSP AERO Screen
        VSPAEROScreen * AeroScreen = dynamic_cast < VSPAEROScreen* >
            ( m_ScreenMgr->GetScreen( vsp::VSP_VSPAERO_SCREEN ) );
        if ( AeroScreen )
        {
            AeroScreen->LoadDrawObjs( drawObjs );
        }

        // Load Render Objects for Geometry Analysis
        GeometryAnalysisScreen * InterfereScreen = dynamic_cast < GeometryAnalysisScreen* >
            ( m_ScreenMgr->GetScreen( vsp::VSP_GEOMETRY_ANALYSIS_SCREEN ) );
        if ( InterfereScreen )
        {
            InterfereScreen->LoadDrawObjs( drawObjs );
        }
    }
}

void VspGlWindow::ResetDrawObjsGeomChangedFlags( vector< DrawObj* > & drawObjs, bool flag )
{
    for( int i = 0; i < (int)drawObjs.size(); i++ )
    {
        drawObjs[i]->m_GeomChanged = flag;
    }
}

void VspGlWindow::ResetDrawObjsGeomChangedFlags( bool flag )
{
    vector<DrawObj *> drawObjs;
    LoadAllDrawObjs( drawObjs );

    ResetDrawObjsGeomChangedFlags( drawObjs, flag );
}

void VspGlWindow::clearScene()
{
    m_GEngine->getScene()->clearScene();
    m_idMap.clear();
    m_GEngine->getDisplay()->getViewport()->clearBackground();
    m_GEngine->getDisplay()->getViewport()->clearFont();
}

void VspGlWindow::UpdateViewportParms()
{
    MainVSPScreen* main = dynamic_cast< MainVSPScreen* >( m_ScreenMgr->GetScreen( vsp::VSP_MAIN_SCREEN ) );

    if ( main )
    {
        VSPGUI::VspGlWindow * glwin = main->GetGLWindow();

        if ( glwin == this )
        {
            Vehicle *veh = VehicleMgr.GetVehicle();
            if ( veh )
            {
                veh->m_ViewportSizeXValue.Set( pixel_w() );
                veh->m_ViewportSizeYValue.Set( pixel_h() );
            }
        }
    }
}

void VspGlWindow::UpdateCORParms()
{
    MainVSPScreen* main = dynamic_cast< MainVSPScreen* >( m_ScreenMgr->GetScreen( vsp::VSP_MAIN_SCREEN ) );

    if ( main )
    {
        VSPGUI::VspGlWindow * glwin = main->GetGLWindow();

        if ( glwin == this )
        {
            Vehicle *veh = VehicleMgr.GetVehicle();

            if ( veh )
            {
                glm::vec3 center = getCOR();

                veh->m_CORXValue.Set( center.x );
                veh->m_CORYValue.Set( center.y );
                veh->m_CORZValue.Set( center.z );
            }
        }
    }
}

void VspGlWindow::UpdatePanParms()
{
    MainVSPScreen* main = dynamic_cast< MainVSPScreen* >( m_ScreenMgr->GetScreen( vsp::VSP_MAIN_SCREEN ) );

    if ( main )
    {
        VSPGUI::VspGlWindow * glwin = main->GetGLWindow();

        if ( glwin == this )
        {
            Vehicle *veh = VehicleMgr.GetVehicle();

            if ( veh )
            {
                glm::vec2 currentPan = getPanValues();
                veh->m_PanXPosValue.Set( currentPan.x );
                veh->m_PanYPosValue.Set( currentPan.y );
            }
        }
    }
}

void VspGlWindow::UpdateZoomParms()
{
    MainVSPScreen* main = dynamic_cast< MainVSPScreen* >( m_ScreenMgr->GetScreen( vsp::VSP_MAIN_SCREEN ) );

    if ( main )
    {
        VSPGUI::VspGlWindow * glwin = main->GetGLWindow();

        if ( glwin == this )
        {
            Vehicle *veh = VehicleMgr.GetVehicle();

            if ( veh )
            {
                veh->m_ZoomValue.Set( getRelativeZoomValue() );
            }
        }
    }
}

void VspGlWindow::UpdateRotationParms()
{
    MainVSPScreen* main = dynamic_cast< MainVSPScreen* >( m_ScreenMgr->GetScreen( vsp::VSP_MAIN_SCREEN ) );

    if ( main )
    {
        VSPGUI::VspGlWindow * glwin = main->GetGLWindow();

        if ( glwin == this )
        {
            Vehicle *veh = VehicleMgr.GetVehicle();

            if ( veh )
            {
                glm::vec3 eulerValues = getRotationEulerAngles();

                veh->m_XRotationValue.Set( eulerValues[0] * ( 180.0 / M_PI ) );
                veh->m_YRotationValue.Set( eulerValues[1] * ( 180.0 / M_PI ) );
                veh->m_ZRotationValue.Set( eulerValues[2] * ( 180.0 / M_PI ) );
            }
        }
    }
}

void VspGlWindow::UpdateAllViewParms()
{
    UpdateViewportParms();
    UpdateCORParms();
    UpdatePanParms();
    UpdateZoomParms();
    UpdateRotationParms();
}

void VspGlWindow::_initGLEW()
{
    if( !m_initialized )
    {
        VSPGraphic::GraphicEngine::initGlew();

        m_initialized = true;
    }
}


// Hand the viewport the color scale that a visible color mapped object asked for, or turn it
// off when nothing did.  The ramp is sampled here so the scale and the surfaces it explains
// are coming from the same function.
void VspGlWindow::_updateColorScale( const std::vector<DrawObj *> &objects )
{
    Display * dis = m_GEngine->getDisplay();

    if ( !dis )
    {
        return;
    }

    VSPGraphic::Viewport * vpt = dis->getViewport();

    if ( !vpt )
    {
        return;
    }

    for( int i = 0; i < (int)objects.size(); i++ )
    {
        if ( objects[i]->m_ColorScaleFlag && objects[i]->m_Visible )
        {
            const int nstop = 17;
            std::vector < glm::vec3 > stops( nstop );

            for ( int k = 0; k < nstop; k++ )
            {
                vec3d c = DrawObj::qualityColorRamp( ( double )k / ( double )( nstop - 1 ) );
                stops[k] = glm::vec3( ( float )c.x(), ( float )c.y(), ( float )c.z() );
            }

            vpt->setColorScale( true, objects[i]->m_ColorScaleTitle,
                                objects[i]->m_ColorScaleLoLabel,
                                objects[i]->m_ColorScaleMidLabel,
                                objects[i]->m_ColorScaleHiLabel, stops );
            return;
        }
    }

    std::vector < glm::vec3 > none;
    vpt->setColorScale( false, "", "", "", "", none );
}

void VspGlWindow::_update( const std::vector<DrawObj *> &objects )
{
    // Check for changes in DrawObjs and adjust accordingly.
    _updateBuffer( objects );

    // A color mapped object asks for a scale to read it by.  Asked for every frame, so it
    // goes away by itself when whatever wanted it stops being drawn.
    _updateColorScale( objects );

    // Get view Z vector for 3D background visibility check.
    Display * dis = m_GEngine->getDisplay();
    if ( dis )
    {
        Viewport * vpt = dis->getViewport();
        if ( vpt )
        {
            Camera * cam = vpt->getCamera();
            if ( cam )
            {
                glm::mat4 mvm = cam->getModelViewMatrix();

                vec3d v( mvm[0][2], mvm[1][2], mvm[2][2] );

                for( int i = 0; i < (int)objects.size(); i++ )
                {
                    if ( objects[i]->m_VisibleDirFlag )
                    {
                        if ( acos( std::abs( dot( objects[ i ]->m_VisibleDir, v ) ) ) * ( 180.0 / M_PI ) > objects[ i ]->m_VisTol )
                        {
                            objects[ i ]->m_Visible = false;
                        }
                    }
                }
            }
        }
    }

    // Process all geometry renderable first, then all labels, pickables, lastly the markers.  Order matters.
    for( int i = 0; i < (int)objects.size(); i++ )
    {
        // If this DrawObj is aimed for other screen, ignore.
        if( objects[i]->m_Screen != m_LinkedScreen )
        {
            continue;
        }

        // Load Settings.
        float red, green, blue;
        float lineWidth;

        red = ( float )objects[i]->m_LineColor.x();
        green = ( float )objects[i]->m_LineColor.y();
        blue = ( float )objects[i]->m_LineColor.z();

        lineWidth = ( float )objects[i]->m_LineWidth;

        unsigned int id;
        ID * idPtr = _findID( objects[i]->m_GeomID );
        if( idPtr )
        {
            id = idPtr->bufferID;
        }
        else
        {
            id = 0xFFFFFFFF;
        }

        VSPGraphic::Entity * eObj;

        switch( objects[i]->m_Type )
        {
        case DrawObj::VSP_SETTING:
            _setLighting( objects[i] );
            break;

        case DrawObj::VSP_CLIP:
            _setClipping( objects[i] );
            break;

        case DrawObj::VSP_WIRE_MESH:
            // Create new scene object if needed.
            if( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_ENTITY, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }
            eObj = dynamic_cast<VSPGraphic::Entity*> ( m_GEngine->getScene()->getObject( id ) );
            if( eObj )
            {
                // Update scene object.
                eObj->setVisibility( objects[i]->m_Visible );
                eObj->setPrimType( Common::VSP_QUADS );
                eObj->setRenderStyle( Common::VSP_DRAW_WIRE_FRAME );
                eObj->setLineColor( red, green, blue );
                eObj->setLineWidth( lineWidth );

                eObj->setMaterial( objects[i]->m_MaterialInfo.Ambient, objects[i]->m_MaterialInfo.Diffuse,
                    objects[i]->m_MaterialInfo.Specular, objects[i]->m_MaterialInfo.Emission,
                    objects[i]->m_MaterialInfo.Shininess );

                if( objects[i]->m_GeomChanged )
                {
                    _loadXSecData( eObj, objects[i] );
                }
            }
            break;

        case DrawObj::VSP_WIRE_TRIS:
            if( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_ENTITY, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }
            eObj = dynamic_cast<VSPGraphic::Entity*> ( m_GEngine->getScene()->getObject( id ) );
            if( eObj )
            {
                eObj->setVisibility( objects[i]->m_Visible );
                eObj->setPrimType( Common::VSP_TRIANGLES );
                eObj->setRenderStyle( Common::VSP_DRAW_WIRE_FRAME );
                eObj->setLineColor( red, green, blue );
                eObj->setLineWidth( lineWidth );

                eObj->setMaterial( objects[i]->m_MaterialInfo.Ambient, objects[i]->m_MaterialInfo.Diffuse,
                    objects[i]->m_MaterialInfo.Specular, objects[i]->m_MaterialInfo.Emission,
                    objects[i]->m_MaterialInfo.Shininess );

                if( objects[i]->m_GeomChanged )
                {
                    _loadTrisData( eObj, objects[i] );
                }
            }
            break;

        case DrawObj::VSP_WIRE_HIDDEN_MESH:
            if( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_ENTITY, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }
            eObj = dynamic_cast<VSPGraphic::Entity*> ( m_GEngine->getScene()->getObject( id ) );
            if( eObj )
            {
                eObj->setVisibility( objects[i]->m_Visible );
                eObj->setPrimType( Common::VSP_QUADS );
                eObj->setRenderStyle( Common::VSP_DRAW_WIRE_FRAME_SOLID );
                eObj->setLineColor( red, green, blue );
                eObj->setLineWidth( lineWidth );

                eObj->setMaterial( objects[i]->m_MaterialInfo.Ambient, objects[i]->m_MaterialInfo.Diffuse,
                    objects[i]->m_MaterialInfo.Specular, objects[i]->m_MaterialInfo.Emission,
                    objects[i]->m_MaterialInfo.Shininess );

                if( objects[i]->m_GeomChanged )
                {
                    _loadXSecData( eObj, objects[i] );
                }
            }
            break;

        case DrawObj::VSP_HIDDEN_TRIS:
            if( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_ENTITY, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }
            eObj = dynamic_cast<VSPGraphic::Entity*> ( m_GEngine->getScene()->getObject( id ) );
            if( eObj )
            {
                eObj->setVisibility( objects[i]->m_Visible );
                eObj->setPrimType( Common::VSP_TRIANGLES );
                eObj->setRenderStyle( Common::VSP_DRAW_SOLID );
                eObj->setLineColor( red, green, blue );
                eObj->setLineWidth( lineWidth );

                eObj->setMaterial( objects[i]->m_MaterialInfo.Ambient, objects[i]->m_MaterialInfo.Diffuse,
                    objects[i]->m_MaterialInfo.Specular, objects[i]->m_MaterialInfo.Emission,
                    objects[i]->m_MaterialInfo.Shininess );

                if( objects[i]->m_GeomChanged )
                {
                    _loadTrisData( eObj, objects[i] );
                }
            }
            break;

        case DrawObj::VSP_WIRE_HIDDEN_TRIS:
            if( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_ENTITY, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }
            eObj = dynamic_cast<VSPGraphic::Entity*> ( m_GEngine->getScene()->getObject( id ) );
            if( eObj )
            {
                eObj->setVisibility( objects[i]->m_Visible );
                eObj->setPrimType( Common::VSP_TRIANGLES );
                eObj->setRenderStyle( Common::VSP_DRAW_WIRE_FRAME_SOLID );
                eObj->setLineColor( red, green, blue );
                eObj->setLineWidth( lineWidth );

                eObj->setMaterial( objects[i]->m_MaterialInfo.Ambient, objects[i]->m_MaterialInfo.Diffuse,
                    objects[i]->m_MaterialInfo.Specular, objects[i]->m_MaterialInfo.Emission,
                    objects[i]->m_MaterialInfo.Shininess );

                if( objects[i]->m_GeomChanged )
                {
                    _loadTrisData( eObj, objects[i] );
                }
            }
            break;

        case DrawObj::VSP_MAPPED_TRIS:
            if( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_ENTITY, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }
            eObj = dynamic_cast<VSPGraphic::Entity*> ( m_GEngine->getScene()->getObject( id ) );
            if( eObj )
            {
                eObj->setVisibility( objects[i]->m_Visible );
                eObj->setPrimType( Common::VSP_TRIANGLES );
                eObj->setRenderStyle( Common::VSP_DRAW_MAPPED );
                eObj->setLineColor( red, green, blue );
                eObj->setLineWidth( lineWidth );

                eObj->setMaterial( objects[i]->m_MaterialInfo.Ambient, objects[i]->m_MaterialInfo.Diffuse,
                    objects[i]->m_MaterialInfo.Specular, objects[i]->m_MaterialInfo.Emission,
                    objects[i]->m_MaterialInfo.Shininess );

                if( objects[i]->m_GeomChanged )
                {
                    _loadTrisData( eObj, objects[i] );
                }
            }
            break;

        case DrawObj::VSP_WIRE_MAPPED_TRIS:
            if( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_ENTITY, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }
            eObj = dynamic_cast<VSPGraphic::Entity*> ( m_GEngine->getScene()->getObject( id ) );
            if( eObj )
            {
                eObj->setVisibility( objects[i]->m_Visible );
                eObj->setPrimType( Common::VSP_TRIANGLES );
                eObj->setRenderStyle( Common::VSP_DRAW_WIRE_FRAME_MAPPED );
                eObj->setLineColor( red, green, blue );
                eObj->setLineWidth( lineWidth );

                eObj->setMaterial( objects[i]->m_MaterialInfo.Ambient, objects[i]->m_MaterialInfo.Diffuse,
                    objects[i]->m_MaterialInfo.Specular, objects[i]->m_MaterialInfo.Emission,
                    objects[i]->m_MaterialInfo.Shininess );

                if( objects[i]->m_GeomChanged )
                {
                    _loadTrisData( eObj, objects[i] );
                }
            }
            break;

        case DrawObj::VSP_CFD_HIDDEN_TRIS:
            if( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_CFD_ENTITY, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }
            eObj = dynamic_cast<VSPGraphic::Entity*> ( m_GEngine->getScene()->getObject( id ) );
            if( eObj )
            {
                eObj->setVisibility( objects[i]->m_Visible );
                eObj->setPrimType( Common::VSP_TRIANGLES );
                eObj->setRenderStyle( Common::VSP_DRAW_WIRE_FRAME_TRANSPARENT_BACK );
                eObj->setLineColor( red, green, blue );
                eObj->setLineWidth( lineWidth );

                eObj->setMaterial( objects[i]->m_MaterialInfo.Ambient, objects[i]->m_MaterialInfo.Diffuse,
                    objects[i]->m_MaterialInfo.Specular, objects[i]->m_MaterialInfo.Emission,
                    objects[i]->m_MaterialInfo.Shininess );

                if( objects[i]->m_GeomChanged )
                {
                    _loadTrisData( eObj, objects[i] );
                }
            }
            break;

        case DrawObj::VSP_WIRE_SHADED_TRIS:
            if( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_CFD_ENTITY, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }
            eObj = dynamic_cast<VSPGraphic::Entity*> ( m_GEngine->getScene()->getObject( id ) );
            if( eObj )
            {
                eObj->setVisibility( objects[i]->m_Visible );
                eObj->setPrimType( Common::VSP_TRIANGLES );
                eObj->setRenderStyle( Common::VSP_DRAW_WIRE_FRAME_SHADED );
                eObj->setLineColor( red, green, blue );
                eObj->setLineWidth( lineWidth );

                eObj->setMaterial( objects[i]->m_MaterialInfo.Ambient, objects[i]->m_MaterialInfo.Diffuse,
                                   objects[i]->m_MaterialInfo.Specular, objects[i]->m_MaterialInfo.Emission,
                                   objects[i]->m_MaterialInfo.Shininess );

                if( objects[i]->m_GeomChanged )
                {
                    _loadTrisData( eObj, objects[i] );
                }
            }
            break;

        case DrawObj::VSP_WIRE_HIDDEN_QUADS:
            if( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_ENTITY, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }
            eObj = dynamic_cast<VSPGraphic::Entity*> ( m_GEngine->getScene()->getObject( id ) );
            if( eObj )
            {
                eObj->setVisibility( objects[i]->m_Visible );
                eObj->setPrimType( Common::VSP_QUADS );
                eObj->setRenderStyle( Common::VSP_DRAW_WIRE_FRAME_SOLID );
                eObj->setLineColor( red, green, blue );
                eObj->setLineWidth( lineWidth );

                eObj->setMaterial( objects[i]->m_MaterialInfo.Ambient, objects[i]->m_MaterialInfo.Diffuse,
                    objects[i]->m_MaterialInfo.Specular, objects[i]->m_MaterialInfo.Emission,
                    objects[i]->m_MaterialInfo.Shininess );

                if( objects[i]->m_GeomChanged )
                {
                    _loadQuadsData( eObj, objects[i] );
                }
            }
            break;

        case DrawObj::VSP_CFD_HIDDEN_QUADS:
            if( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_CFD_ENTITY, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }
            eObj = dynamic_cast<VSPGraphic::Entity*> ( m_GEngine->getScene()->getObject( id ) );
            if( eObj )
            {
                eObj->setVisibility( objects[i]->m_Visible );
                eObj->setPrimType( Common::VSP_QUADS );
                eObj->setRenderStyle( Common::VSP_DRAW_WIRE_FRAME_TRANSPARENT_BACK );
                eObj->setLineColor( red, green, blue );
                eObj->setLineWidth( lineWidth );

                eObj->setMaterial( objects[i]->m_MaterialInfo.Ambient, objects[i]->m_MaterialInfo.Diffuse,
                    objects[i]->m_MaterialInfo.Specular, objects[i]->m_MaterialInfo.Emission,
                    objects[i]->m_MaterialInfo.Shininess );

                if( objects[i]->m_GeomChanged )
                {
                    _loadQuadsData( eObj, objects[i] );
                }
            }
            break;

        case DrawObj::VSP_SHADED_QUADS:
            if ( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_ENTITY, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }
            eObj = dynamic_cast<VSPGraphic::Entity*> ( m_GEngine->getScene()->getObject( id ) );
            if ( eObj )
            {
                eObj->setVisibility( objects[i]->m_Visible );
                eObj->setPrimType( Common::VSP_QUADS );
                eObj->setRenderStyle( Common::VSP_DRAW_SHADED );

                eObj->setMaterial( objects[i]->m_MaterialInfo.Ambient, objects[i]->m_MaterialInfo.Diffuse,
                                   objects[i]->m_MaterialInfo.Specular, objects[i]->m_MaterialInfo.Emission,
                                   objects[i]->m_MaterialInfo.Shininess );

                if ( objects[i]->m_GeomChanged )
                {
                    _loadQuadsData( eObj, objects[i] );
                }
            }
            break;

        case DrawObj::VSP_WIRE_QUADS:
            if ( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_ENTITY, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }
            eObj = dynamic_cast<VSPGraphic::Entity*> ( m_GEngine->getScene()->getObject( id ) );
            if ( eObj )
            {
                eObj->setVisibility( objects[i]->m_Visible );
                eObj->setPrimType( Common::VSP_QUADS );
                eObj->setRenderStyle( Common::VSP_DRAW_WIRE_FRAME );
                eObj->setLineColor( red, green, blue );
                eObj->setLineWidth( lineWidth );

                eObj->setMaterial( objects[i]->m_MaterialInfo.Ambient, objects[i]->m_MaterialInfo.Diffuse,
                                   objects[i]->m_MaterialInfo.Specular, objects[i]->m_MaterialInfo.Emission,
                                   objects[i]->m_MaterialInfo.Shininess );

                if ( objects[i]->m_GeomChanged )
                {
                    _loadQuadsData( eObj, objects[i] );
                }
            }
            break;

        case DrawObj::VSP_WIRE_SHADED_QUADS:
            if ( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_ENTITY, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }
            eObj = dynamic_cast<VSPGraphic::Entity*> ( m_GEngine->getScene()->getObject( id ) );
            if ( eObj )
            {
                eObj->setVisibility( objects[i]->m_Visible );
                eObj->setPrimType( Common::VSP_QUADS );
                eObj->setRenderStyle( Common::VSP_DRAW_WIRE_FRAME_SHADED );
                eObj->setLineColor( red, green, blue );
                eObj->setLineWidth( lineWidth );

                eObj->setMaterial( objects[i]->m_MaterialInfo.Ambient, objects[i]->m_MaterialInfo.Diffuse,
                                   objects[i]->m_MaterialInfo.Specular, objects[i]->m_MaterialInfo.Emission,
                                   objects[i]->m_MaterialInfo.Shininess );

                if ( objects[i]->m_GeomChanged )
                {
                    _loadQuadsData( eObj, objects[i] );
                }
            }
            break;

        case DrawObj::VSP_MAPPED_QUADS:
            if ( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_ENTITY, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }
            eObj = dynamic_cast<VSPGraphic::Entity*> ( m_GEngine->getScene()->getObject( id ) );
            if ( eObj )
            {
                eObj->setVisibility( objects[i]->m_Visible );
                eObj->setPrimType( Common::VSP_QUADS );
                eObj->setRenderStyle( Common::VSP_DRAW_MAPPED );
                eObj->setLineColor( red, green, blue );
                eObj->setLineWidth( lineWidth );

                eObj->setMaterial( objects[i]->m_MaterialInfo.Ambient, objects[i]->m_MaterialInfo.Diffuse,
                                   objects[i]->m_MaterialInfo.Specular, objects[i]->m_MaterialInfo.Emission,
                                   objects[i]->m_MaterialInfo.Shininess );

                if ( objects[i]->m_GeomChanged )
                {
                    _loadQuadsData( eObj, objects[i] );
                }
            }
            break;

        case DrawObj::VSP_WIRE_MAPPED_QUADS:
            if ( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_ENTITY, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }
            eObj = dynamic_cast<VSPGraphic::Entity*> ( m_GEngine->getScene()->getObject( id ) );
            if ( eObj )
            {
                eObj->setVisibility( objects[i]->m_Visible );
                eObj->setPrimType( Common::VSP_QUADS );
                eObj->setRenderStyle( Common::VSP_DRAW_WIRE_FRAME_MAPPED );
                eObj->setLineColor( red, green, blue );
                eObj->setLineWidth( lineWidth );

                eObj->setMaterial( objects[i]->m_MaterialInfo.Ambient, objects[i]->m_MaterialInfo.Diffuse,
                                   objects[i]->m_MaterialInfo.Specular, objects[i]->m_MaterialInfo.Emission,
                                   objects[i]->m_MaterialInfo.Shininess );

                if ( objects[i]->m_GeomChanged )
                {
                    _loadQuadsData( eObj, objects[i] );
                }
            }
            break;

        case DrawObj::VSP_SHADED_MESH:
            if( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_ENTITY, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }
            eObj = dynamic_cast<VSPGraphic::Entity*> ( m_GEngine->getScene()->getObject( id ) );
            if( eObj )
            {
                eObj->setVisibility( objects[i]->m_Visible );
                eObj->setPrimType( Common::VSP_QUADS );
                eObj->setRenderStyle( Common::VSP_DRAW_SHADED );

                eObj->setMaterial( objects[i]->m_MaterialInfo.Ambient, objects[i]->m_MaterialInfo.Diffuse,
                    objects[i]->m_MaterialInfo.Specular, objects[i]->m_MaterialInfo.Emission,
                    objects[i]->m_MaterialInfo.Shininess );

                if( objects[i]->m_GeomChanged )
                {
                    _loadXSecData( eObj, objects[i] );
                }
            }
            break;

        case DrawObj::VSP_SHADED_TRIS:
            if( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_ENTITY, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }
            eObj = dynamic_cast<VSPGraphic::Entity*> ( m_GEngine->getScene()->getObject( id ) );
            if( eObj )
            {
                eObj->setVisibility( objects[i]->m_Visible );
                eObj->setPrimType( Common::VSP_TRIANGLES );
                eObj->setRenderStyle( Common::VSP_DRAW_SHADED );

                eObj->setMaterial( objects[i]->m_MaterialInfo.Ambient, objects[i]->m_MaterialInfo.Diffuse,
                    objects[i]->m_MaterialInfo.Specular, objects[i]->m_MaterialInfo.Emission,
                    objects[i]->m_MaterialInfo.Shininess );

                if( objects[i]->m_GeomChanged )
                {
                    _loadTrisData( eObj, objects[i] );
                }
            }
            break;

        case DrawObj::VSP_TEXTURED_MESH:
            if( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_ENTITY, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }
            eObj = dynamic_cast<VSPGraphic::Entity*> ( m_GEngine->getScene()->getObject( id ) );
            if( eObj )
            {
                eObj->setVisibility( objects[i]->m_Visible );
                eObj->setPrimType( Common::VSP_QUADS );
                eObj->setRenderStyle( Common::VSP_DRAW_TEXTURED );

                eObj->setMaterial( objects[i]->m_MaterialInfo.Ambient, objects[i]->m_MaterialInfo.Diffuse,
                    objects[i]->m_MaterialInfo.Specular, objects[i]->m_MaterialInfo.Emission,
                    objects[i]->m_MaterialInfo.Shininess );

                if( objects[i]->m_GeomChanged )
                {
                    _loadXSecData( eObj, objects[i] );
                }
                _updateTextures( objects[i] );
            }
            break;

        case DrawObj::VSP_TEXTURED_MESH_FLAT:
            if( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_ENTITY, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }
            eObj = dynamic_cast<VSPGraphic::Entity*> ( m_GEngine->getScene()->getObject( id ) );
            if( eObj )
            {
                eObj->setLighting( nullptr );
                eObj->setVisibility( objects[i]->m_Visible );
                eObj->setPrimType( Common::VSP_QUADS );
                eObj->setRenderStyle( Common::VSP_DRAW_TEXTURED );

                eObj->setMaterial( objects[i]->m_MaterialInfo.Ambient, objects[i]->m_MaterialInfo.Diffuse,
                    objects[i]->m_MaterialInfo.Specular, objects[i]->m_MaterialInfo.Emission,
                    objects[i]->m_MaterialInfo.Shininess );

                if( objects[i]->m_GeomChanged )
                {
                    _loadXSecData( eObj, objects[i] );
                }
                _updateTextures( objects[i] );
            }
            break;

        case DrawObj::VSP_TEXTURED_MESH_FLAT_TRANSPARENT_BACK:
            if( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_ENTITY, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }
            eObj = dynamic_cast<VSPGraphic::Entity*> ( m_GEngine->getScene()->getObject( id ) );
            if( eObj )
            {
                eObj->setLighting( nullptr );
                eObj->setVisibility( objects[i]->m_Visible );
                eObj->setPrimType( Common::VSP_QUADS );
                eObj->setRenderStyle( Common::VSP_DRAW_TEXTURED_TRANSPARENT_BACK );

                eObj->setMaterial( objects[i]->m_MaterialInfo.Ambient, objects[i]->m_MaterialInfo.Diffuse,
                    objects[i]->m_MaterialInfo.Specular, objects[i]->m_MaterialInfo.Emission,
                    objects[i]->m_MaterialInfo.Shininess );

                if( objects[i]->m_GeomChanged )
                {
                    _loadXSecData( eObj, objects[i] );
                }
                _updateTextures( objects[i] );
            }
            break;

        default:
            break;
        }
    }

    // Process Labels.  Order Matters.
    for( int i = 0; i < ( int )objects.size(); i++ )
    {
        // If this DrawObj is aimed for other screen, ignore.
        if( objects[i]->m_Screen != m_LinkedScreen )
        {
            continue;
        }

        unsigned int id;
        ID * idPtr = _findID( objects[i]->m_GeomID );
        if( idPtr )
        {
            id = idPtr->bufferID;
        }
        else
        {
            id = 0xFFFFFFFF;
        }

        VSPGraphic::Probe * probe;
        VSPGraphic::Ruler * ruler;
        VSPGraphic::Protractor * protractor;
        VSPGraphic::Routing * routing;

        switch( objects[i]->m_Type )
        {
        case DrawObj::VSP_PROTRACTOR:
            if( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_PROTRACTOR, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }
            protractor = dynamic_cast<VSPGraphic::Protractor*> ( m_GEngine->getScene()->getObject( id ) );
            if( protractor )
            {
                protractor->setVisibility( objects[i]->m_Visible );

                protractor->setTextColor( (float)objects[i]->m_TextColor.x(),
                    (float)objects[i]->m_TextColor.y(),
                    (float)objects[i]->m_TextColor.z() );

                protractor->setTextSize( (float)objects[i]->m_TextSize );

                glm::vec3 start, mid, end, label_dir;

                switch( objects[i]->m_Protractor.Step )
                {
                case DrawObj::VSP_PROTRACTOR_STEP_ZERO:
                    protractor->reset();
                    break;

                case DrawObj::VSP_PROTRACTOR_STEP_ONE:
                    start = glm::vec3( objects[i]->m_Protractor.Start.x(),
                        objects[i]->m_Protractor.Start.y(),
                        objects[i]->m_Protractor.Start.z() );
                    protractor->placeProtractor( start );
                    break;

                case DrawObj::VSP_PROTRACTOR_STEP_TWO:
                    start = glm::vec3( objects[i]->m_Protractor.Start.x(),
                        objects[i]->m_Protractor.Start.y(),
                        objects[i]->m_Protractor.Start.z() );
                    mid = glm::vec3( objects[i]->m_Protractor.Mid.x(),
                        objects[i]->m_Protractor.Mid.y(),
                        objects[i]->m_Protractor.Mid.z() );
                    protractor->placeProtractor( start, mid );
                    break;

                case DrawObj::VSP_PROTRACTOR_STEP_THREE:
                    start = glm::vec3( objects[i]->m_Protractor.Start.x(),
                        objects[i]->m_Protractor.Start.y(),
                        objects[i]->m_Protractor.Start.z() );
                    mid = glm::vec3( objects[i]->m_Protractor.Mid.x(),
                        objects[i]->m_Protractor.Mid.y(),
                        objects[i]->m_Protractor.Mid.z() );
                    end = glm::vec3( objects[i]->m_Protractor.End.x(),
                        objects[i]->m_Protractor.End.y(),
                        objects[i]->m_Protractor.End.z() );
                    label_dir = glm::vec3( objects[i]->m_Protractor.Label_Dir.x(),
                                           objects[i]->m_Protractor.Label_Dir.y(),
                                           objects[i]->m_Protractor.Label_Dir.z() );
                    protractor->placeProtractor( start, mid, end, label_dir, objects[i]->m_Protractor.Label );
                    break;

                case DrawObj::VSP_PROTRACTOR_STEP_COMPLETE:
                    start = glm::vec3( objects[i]->m_Protractor.Start.x(),
                        objects[i]->m_Protractor.Start.y(),
                        objects[i]->m_Protractor.Start.z() );
                    mid = glm::vec3( objects[i]->m_Protractor.Mid.x(),
                        objects[i]->m_Protractor.Mid.y(),
                        objects[i]->m_Protractor.Mid.z() );
                    end = glm::vec3( objects[i]->m_Protractor.End.x(),
                        objects[i]->m_Protractor.End.y(),
                        objects[i]->m_Protractor.End.z() );
                    label_dir = glm::vec3( objects[i]->m_Protractor.Label_Dir.x(),
                                           objects[i]->m_Protractor.Label_Dir.y(),
                                           objects[i]->m_Protractor.Label_Dir.z() );
                    protractor->placeProtractor( start, mid, end, label_dir, objects[i]->m_Protractor.Offset, objects[i]->m_Protractor.Label, objects[i]->m_Protractor.Dir );
                    break;

                default:
                    break;
                }
            }
            break;

        case DrawObj::VSP_RULER:
            if( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_RULER, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }
            ruler = dynamic_cast<VSPGraphic::Ruler*> ( m_GEngine->getScene()->getObject( id ) );
            if( ruler )
            {
                ruler->setVisibility( objects[i]->m_Visible );

                ruler->setTextColor( (float)objects[i]->m_TextColor.x(),
                    (float)objects[i]->m_TextColor.y(),
                    (float)objects[i]->m_TextColor.z() );

                ruler->setTextSize( (float)objects[i]->m_TextSize );

                glm::vec3 start, end, offset;

                switch( objects[i]->m_Ruler.Step )
                {
                case DrawObj::VSP_RULER_STEP_ZERO:
                    ruler->reset();
                    break;

                case DrawObj::VSP_RULER_STEP_ONE:
                    start = glm::vec3( objects[i]->m_Ruler.Start.x(),
                        objects[i]->m_Ruler.Start.y(),
                        objects[i]->m_Ruler.Start.z() );
                    ruler->placeRuler( start );
                    break;

                case DrawObj::VSP_RULER_STEP_TWO:
                    start = glm::vec3( objects[i]->m_Ruler.Start.x(),
                        objects[i]->m_Ruler.Start.y(),
                        objects[i]->m_Ruler.Start.z() );
                    end = glm::vec3( objects[i]->m_Ruler.End.x(),
                        objects[i]->m_Ruler.End.y(),
                        objects[i]->m_Ruler.End.z() );
                    ruler->placeRuler( start, end, objects[i]->m_Ruler.Label );
                    break;

                case DrawObj::VSP_RULER_STEP_COMPLETE:
                    start = glm::vec3( objects[i]->m_Ruler.Start.x(),
                        objects[i]->m_Ruler.Start.y(),
                        objects[i]->m_Ruler.Start.z() );
                    end = glm::vec3( objects[i]->m_Ruler.End.x(),
                        objects[i]->m_Ruler.End.y(),
                        objects[i]->m_Ruler.End.z() );
                    offset = glm::vec3( objects[i]->m_Ruler.Offset.x(),
                        objects[i]->m_Ruler.Offset.y(),
                        objects[i]->m_Ruler.Offset.z() );
                    ruler->placeRuler( start, end, offset, objects[i]->m_Ruler.Label, objects[i]->m_Ruler.Dir );
                    break;

                default:
                    break;
                }
            }
            break;

        case DrawObj::VSP_PROBE:
            if( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_PROBE, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }
            probe = dynamic_cast<VSPGraphic::Probe*> ( m_GEngine->getScene()->getObject( id ) );
            if( probe )
            {
                probe->setVisibility( objects[i]->m_Visible );

                probe->setTextColor( (float)objects[i]->m_TextColor.x(),
                    (float)objects[i]->m_TextColor.y(),
                    (float)objects[i]->m_TextColor.z() );

                probe->setTextSize( (float)objects[i]->m_TextSize );

                glm::vec3 pt, norm;
                float len;

                switch( objects[i]->m_Probe.Step )
                {
                case DrawObj::VSP_PROBE_STEP_ZERO:
                    probe->reset();
                    break;

                case DrawObj::VSP_PROBE_STEP_ONE:
                    pt = glm::vec3( objects[i]->m_Probe.Pt.x(),
                        objects[i]->m_Probe.Pt.y(),
                        objects[i]->m_Probe.Pt.z() );
                    norm = glm::vec3( objects[i]->m_Probe.Norm.x(),
                        objects[i]->m_Probe.Norm.y(),
                        objects[i]->m_Probe.Norm.z() );
                    probe->placeProbe( pt, norm, objects[i]->m_Probe.Label );
                    break;

                case DrawObj::VSP_PROBE_STEP_COMPLETE:
                    pt = glm::vec3( objects[i]->m_Probe.Pt.x(),
                        objects[i]->m_Probe.Pt.y(),
                        objects[i]->m_Probe.Pt.z() );
                    norm = glm::vec3( objects[i]->m_Probe.Norm.x(),
                        objects[i]->m_Probe.Norm.y(),
                        objects[i]->m_Probe.Norm.z() );
                    len = objects[i]->m_Probe.Len;
                    probe->placeProbe( pt, norm, len, objects[i]->m_Probe.Label );
                    break;

                default:
                    break;
                }
            }
            break;

        case DrawObj::VSP_ROUTING:
            if( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_ROUTING, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }
            routing = dynamic_cast<VSPGraphic::Routing*> ( m_GEngine->getScene()->getObject( id ) );
            if( ruler )
            {
                routing->setVisibility( objects[i]->m_Visible );

                routing->setLiveIndex( objects[i]->m_Routing.LiveIndex );

                if( objects[i]->m_GeomChanged )
                {
                    routing->_v.clear();
                    int npt = objects[i]->m_PntVec.size();
                    routing->_v.reserve( npt );

                    for ( int j = 0; j < npt; j++ )
                    {
                        vec3d &pt = objects[i]->m_PntVec[j];
                        routing->_v.push_back( glm::vec3( pt.v[0], pt.v[1], pt.v[2] ) );
                    }

                }
            }
            break;

        default:
            break;
       }
    }

    // Now process all pickables.  Order matters.

    // Reset flag to show all selections.  This prevents selections from permanently hidden
    // due to misuse of VSP_PICK_VERTEX_HIDE_SELECTION object.
    m_GEngine->getScene()->showSelection();

    for( int i = 0; i < ( int )objects.size(); i++ )
    {
        // If this DrawObj is aimed for other screen, ignore.
        if( objects[i]->m_Screen != m_LinkedScreen )
        {
            continue;
        }

        unsigned int id;
        ID * idPtr = _findID( objects[i]->m_GeomID );
        if( idPtr )
        {
            id = idPtr->bufferID;
        }
        else
        {
            id = 0xFFFFFFFF;
        }

        Pickable * pObj;
        PickablePnts * ppntObj;

        switch( objects[i]->m_Type )
        {
        case DrawObj::VSP_PICK_GEOM:
            if( id == 0xFFFFFFFF )
            {
                ID * sourceId = _findID( objects[i]->m_PickSourceID );
                if( sourceId )
                {
                    m_GEngine->getScene()->createObject( Common::VSP_OBJECT_PICK_GEOM, &id, sourceId->bufferID );

                    ID idInfo;
                    idInfo.bufferID = id;
                    idInfo.geomID = objects[i]->m_GeomID;
                    m_idMap[ idInfo.geomID ] = idInfo;
                }
            }
            pObj = dynamic_cast<Pickable*> ( m_GEngine->getScene()->getObject( id ) );
            if( pObj )
            {
                pObj->setVisibility( objects[i]->m_Visible );
                pObj->setGroup( objects[i]->m_FeedbackGroup );
                pObj->update();
            }
            break;

        case DrawObj::VSP_PICK_VERTEX:
            if( id == 0xFFFFFFFF )
            {
                ID * sourceId = _findID( objects[i]->m_PickSourceID );
                if(sourceId)
                {
                    m_GEngine->getScene()->createObject( Common::VSP_OBJECT_PICK_VERTEX, &id, sourceId->bufferID );

                    ID idInfo;
                    idInfo.bufferID = id;
                    idInfo.geomID = objects[i]->m_GeomID;
                    m_idMap[ idInfo.geomID ] = idInfo;
                }
            }
            ppntObj = dynamic_cast<PickablePnts*> ( m_GEngine->getScene()->getObject( id ) );
            if( ppntObj )
            {
                ppntObj->setVisibility( objects[i]->m_Visible );
                ppntObj->setGroup( objects[i]->m_FeedbackGroup );
                ppntObj->setPointSize( objects[i]->m_PointSize );
                ppntObj->update();
            }
            break;

        case DrawObj::VSP_PICK_LOCATION:
            if( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_PICK_LOCATION, &id, 0 );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }
            pObj = dynamic_cast<Pickable*> ( m_GEngine->getScene()->getObject( id ) );
            if( pObj )
            {
                pObj->setVisibility( objects[i]->m_Visible );
                pObj->setGroup( objects[i]->m_FeedbackGroup );
                pObj->update();
            }
            break;

        default:
            break;
        }
    }

    // Now process all markers.  Order matters.
    for( int i = 0; i < ( int )objects.size(); i++ )
    {
        // If this DrawObj is aimed for other screen, ignore.
        if( objects[i]->m_Screen != m_LinkedScreen )
        {
            continue;
        }

        // Load Settings.
        float red, green, blue;
        float lineWidth;

        int stippleFactor;
        int stipplePattern;
        bool stippleFlag;

        red = ( float )objects[i]->m_LineColor.x();
        green = ( float )objects[i]->m_LineColor.y();
        blue = ( float )objects[i]->m_LineColor.z();

        lineWidth = ( float )objects[i]->m_LineWidth;

        stippleFactor = objects[i]->m_StippleFactor;
        stipplePattern = objects[i]->m_StipplePattern;
        stippleFlag = objects[i]->m_StippleFlag;

        unsigned int id;
        ID * idPtr = _findID( objects[i]->m_GeomID );
        if( idPtr )
        {
            id = idPtr->bufferID;
        }
        else
        {
            id = 0xFFFFFFFF;
        }

        Renderable * rObj;

        switch( objects[i]->m_Type )
        {
        case DrawObj::VSP_POINTS:
            if( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_MARKER, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }

            rObj = dynamic_cast<Renderable*> ( m_GEngine->getScene()->getObject( id ) );
            if( rObj )
            {
                rObj->setVisibility( objects[i]->m_Visible );
                rObj->setPrimType( VSPGraphic::Common::VSP_POINTS );
                rObj->setPointColor( objects[i]->m_PointColor.x(),
                    objects[i]->m_PointColor.y(), objects[i]->m_PointColor.z() );
                rObj->setPointSize( objects[i]->m_PointSize );

                // Update buffer data if needed.
                if( objects[i]->m_GeomChanged )
                {
                    _loadMarkData( rObj, objects[i] );
                }
            }
            break;

        case DrawObj::VSP_LINES:
            if( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_MARKER, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }

            rObj = dynamic_cast<Renderable*> ( m_GEngine->getScene()->getObject( id ) );
            if( rObj )
            {
                rObj->setVisibility( objects[i]->m_Visible );
                rObj->setPrimType( VSPGraphic::Common::VSP_LINES );
                rObj->setLineColor( red, green, blue, ( float )objects[i]->m_LineAlpha );
                rObj->setLineWidth( lineWidth );
                rObj->setStippleFactor( stippleFactor );
                rObj->setStipplePattern( stipplePattern );
                rObj->setStippleFlag( stippleFlag );

                if( objects[i]->m_GeomChanged )
                {
                    _loadMarkData( rObj, objects[i] );
                }
            }
            break;

        case DrawObj::VSP_LINE_LOOP:
            if( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_MARKER, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }
            rObj = dynamic_cast<Renderable*> ( m_GEngine->getScene()->getObject( id ) );
            if( rObj )
            {
                rObj->setVisibility( objects[i]->m_Visible );
                rObj->setPrimType( VSPGraphic::Common::VSP_LINE_LOOP );
                rObj->setLineColor( red, green, blue );
                rObj->setLineWidth( lineWidth );
                rObj->setStippleFactor( stippleFactor );
                rObj->setStipplePattern( stipplePattern );
                rObj->setStippleFlag( stippleFlag );

                if( objects[i]->m_GeomChanged )
                {
                    _loadMarkData( rObj, objects[i] );
                }
            }
            break;

        case DrawObj::VSP_LINE_STRIP:
            if( id == 0xFFFFFFFF )
            {
                m_GEngine->getScene()->createObject( Common::VSP_OBJECT_MARKER, &id );

                ID idInfo;
                idInfo.bufferID = id;
                idInfo.geomID = objects[i]->m_GeomID;
                m_idMap[ idInfo.geomID ] = idInfo;
            }
            rObj = dynamic_cast<Renderable*> ( m_GEngine->getScene()->getObject( id ) );
            if( rObj )
            {
                rObj->setVisibility( objects[i]->m_Visible );
                rObj->setPrimType( VSPGraphic::Common::VSP_LINE_STRIP );
                rObj->setLineColor( red, green, blue );
                rObj->setLineWidth( lineWidth );
                rObj->setStippleFactor( stippleFactor );
                rObj->setStipplePattern( stipplePattern );
                rObj->setStippleFlag( stippleFlag );

                if( objects[i]->m_GeomChanged )
                {
                    _loadMarkData( rObj, objects[i] );
                }
            }
            break;

        default:
            break;
        }
    }
}

VspGlWindow::ID * VspGlWindow::_findID( const std::string &geomID )
{
    auto it = m_idMap.find( geomID );
    if( it == m_idMap.end() )
    {
        return nullptr;
    }
    return &( it->second );
}

VspGlWindow::ID * VspGlWindow::_findID( unsigned int bufferID )
{
    for ( auto it = m_idMap.begin(); it != m_idMap.end(); ++it )
    {
        if ( it->second.bufferID ==  bufferID )
        {
            return &( it->second );
        }
    }
    return nullptr;
}

void VspGlWindow::_updateBuffer( const std::vector<DrawObj *> &objects )
{
    std::unordered_map< std::string, DrawObj * > objects_map;
    for( int j = 0; j < ( int )objects.size(); j++ )
    {
        objects_map[ objects[j]->m_GeomID ] = objects[j];
    }

    // Copy map ID's into vector so we can iterate on vector while removing from map.
    std::vector < std::string > idvec;
    idvec.reserve( m_idMap.size() );
    for ( auto mit = m_idMap.begin(); mit != m_idMap.end(); ++mit )
    {
        idvec.push_back( mit->first );
    }

    // Figure out if any buffer object no longer exists in DrawObjs.
    for ( int i = 0; i < idvec.size(); i++ )
    {
        const string &id = idvec[i];
        if ( id != std::string( "Default" ) )
        {
            auto it = objects_map.find( id );
            if( it == objects_map.end() )
            {
                m_GEngine->getScene()->removeObject( m_idMap[ id ].bufferID );
                m_idMap.erase( id );
            }
        }
    }
}

void VspGlWindow::_updateTextures( DrawObj * drawObj )
{
    VspGlWindow::ID * id = _findID( drawObj->m_GeomID );
    if( !id )
    {
        // Can't find Object that is linked to this drawObj
        assert( false );
        return;
    }

    Renderable * renderable = dynamic_cast<Renderable*>( m_GEngine->getScene()->getObject( id->bufferID ) );
    if( renderable )
    {
        VSPGraphic::Entity * entity = dynamic_cast<VSPGraphic::Entity *>( renderable );
        if( entity )
        {
            // Check if any textureID no long exists in drawObj.
            // Remove Deleted Textures.

            std::vector<TextureID> texToRemove;
            std::vector<TextureID> texToKeep;

            for( int i = 0; i < ( int )id->textureIDs.size(); i++ )
            {
                bool exist = false;
                for( int j = 0; j < ( int )drawObj->m_TextureInfos.size(); j++ )
                {
                    if( id->textureIDs[i].geomTexID == drawObj->m_TextureInfos[j].ID )
                    {
                        texToKeep.push_back( id->textureIDs[i] );
                        exist = true;
                        break;
                    }
                }
                if( !exist )
                {
                    texToRemove.push_back( id->textureIDs[i] );
                }
            }

            for(int i = 0; i < ( int )texToRemove.size(); i++)
            {
                entity->getTextureMgr()->remove( texToRemove[i].bufferTexID );
            }

            id->textureIDs = texToKeep;

            // Update / add textures.

            std::vector<DrawObj::TextureInfo> drawObjTexList = drawObj->m_TextureInfos;
            for( int i = 0; i < ( int )drawObjTexList.size(); i++ )
            {
                unsigned int texBufferID;

                TextureID * texID = id->find( drawObjTexList[i].ID );
                if( texID )
                {
                    texBufferID = texID->bufferTexID;
                }
                else
                {
                    // New texture.
                    TextureID newTex;
                    newTex.geomTexID = drawObjTexList[i].ID;
                    newTex.bufferTexID = entity->getTextureMgr()->add(
                            VSPGraphic::GlobalTextureRepo()->get2DTexture(
                                    drawObjTexList[i].FileName.c_str(),
                                    drawObjTexList[i].ModificationKey,
                                    drawObjTexList[i].Rot,
                                    drawObjTexList[i].FlipLR,
                                    drawObjTexList[i].FlipUD,
                                    drawObjTexList[i].AutoTrans ) );
                    id->textureIDs.push_back( newTex );

                    texBufferID = newTex.bufferTexID;
                }

                float UScale = ( float )( 1 / drawObjTexList[i].UScale );
                float WScale = ( float )( 1 / drawObjTexList[i].WScale );
                float U = ( float )( drawObjTexList[i].U * -1.f * UScale );
                float W = ( float )( drawObjTexList[i].W * WScale );

                // Update Texture Properties.
                entity->getTextureMgr()->translate( texBufferID, U, W );
                entity->getTextureMgr()->scale( texBufferID, UScale, WScale );
                entity->getTextureMgr()->transparentize( texBufferID, drawObjTexList[ i ].Transparency, drawObjTexList[ i ].BlendTransparency );
                entity->getTextureMgr()->flipCoord( texBufferID, drawObjTexList[i].UFlip, drawObjTexList[i].WFlip );
            }
        }
        else
        {
            // Render Object is not type of Textureable Entity.
            assert( false );
        }
    }
    else
    {
        // Can't find Render Object.
        assert( false );
    }
}

void VspGlWindow::_loadXSecData( Renderable * destObj, DrawObj * drawObj )
{
    std::vector<float> vdata;
    std::vector<unsigned int> edata;

    // Generate Texture Coordinate.
//    std::vector<std::vector<vec3d>> textureCoords = _generateTexCoordFromXSec( drawObj );

    int num_mesh = drawObj->m_PntMesh.size();

    if ( drawObj->m_NormMesh.size() != num_mesh || drawObj->m_uTexMesh.size() != num_mesh || drawObj->m_vTexMesh.size() != num_mesh )
    {
        destObj->emptyVBuffer();
        destObj->emptyEBuffer();
        destObj->enableEBuffer( false );
        return;
    }

    unsigned int vtotal = 0;
    unsigned int etotal = 0;

    for ( int k = 0; k < num_mesh; k++ )
    {
        int num_pnts = drawObj->m_PntMesh[k].size();
        int num_xsecs = 0;
        if ( num_pnts )
            num_xsecs = drawObj->m_PntMesh[k][0].size();

        vtotal += num_xsecs * num_pnts;

        etotal += (num_pnts - 1) * (num_xsecs - 1);
    }

    vdata.reserve( vtotal * 8 );
    edata.reserve( etotal * 4 );

    unsigned int offset = 0;

    for ( int k = 0; k < num_mesh; k++ )
    {
        int increment = 0;

        int num_pnts = drawObj->m_PntMesh[k].size();
        int num_xsecs = 0;
        if ( num_pnts )
            num_xsecs = drawObj->m_PntMesh[k][0].size();

        // Vertex Buffer.
        for ( int i = 0 ; i < num_pnts ; i++ )
        {
            for ( int j = 0 ; j < num_xsecs ; j++ )
            {
                vdata.push_back( (float)drawObj->m_PntMesh[k][i][j].x() );
                vdata.push_back( (float)drawObj->m_PntMesh[k][i][j].y() );
                vdata.push_back( (float)drawObj->m_PntMesh[k][i][j].z() );

                vdata.push_back( (float)drawObj->m_NormMesh[k][i][j].x() );
                vdata.push_back( (float)drawObj->m_NormMesh[k][i][j].y() );
                vdata.push_back( (float)drawObj->m_NormMesh[k][i][j].z() );

                vdata.push_back( (float)drawObj->m_uTexMesh[k][i][j] );
                vdata.push_back( (float)drawObj->m_vTexMesh[k][i][j] );
                increment++;
            }
        }


        // Element Buffer.
        for( int i = 0; i < num_pnts - 1; i++ )
        {
            for( int j = 0; j < num_xsecs - 1; j++ )
            {
                edata.push_back( offset + i * num_xsecs + j );
                edata.push_back( offset + ( i + 1 ) * num_xsecs + j );
                edata.push_back( offset + ( i + 1 ) * num_xsecs + j + 1 );
                edata.push_back( offset + i * num_xsecs + j + 1 );
            }
        }

        offset += increment;

    }

    destObj->setFacingCW( drawObj->m_FlipNormals );

    destObj->emptyVBuffer();
    destObj->appendVBuffer( vdata.data(), sizeof(float) * vdata.size() );

    destObj->emptyEBuffer();
    destObj->appendEBuffer( edata.data(), sizeof( unsigned int ) * edata.size() );
    destObj->enableEBuffer( true );

}

void VspGlWindow::_loadTrisData( Renderable * destObj, DrawObj * drawObj )
{
    assert( drawObj->m_PntVec.size() == drawObj->m_NormVec.size() );

    unsigned int n = drawObj->m_PntVec.size();

    std::vector<float> data( n * 8, 0.0f );

    for ( int i = 0; i < ( int )drawObj->m_PntVec.size(); i++ )
    {
        const int j = i * 8;
        data[ j + 0 ] = ( float )drawObj->m_PntVec[i].x();
        data[ j + 1 ] = ( float )drawObj->m_PntVec[i].y();
        data[ j + 2 ] = ( float )drawObj->m_PntVec[i].z();

        data[ j + 3 ] = ( float )drawObj->m_NormVec[i].x();
        data[ j + 4 ] = ( float )drawObj->m_NormVec[i].y();
        data[ j + 5 ] = ( float )drawObj->m_NormVec[i].z();
    }
    destObj->setFacingCW( drawObj->m_FlipNormals );

    destObj->emptyVBuffer();
    destObj->appendVBuffer( data.data(), sizeof( float ) * data.size() );

    if ( drawObj->m_WireColorVec.size() == drawObj->m_PntVec.size() )
    {
        std::vector<float> cdata( n * 4, 0.0f );

        for ( int i = 0; i < ( int )drawObj->m_WireColorVec.size(); i++ )
        {
            const int j = i * 4;
            cdata[ j + 0 ] = ( float )drawObj->m_WireColorVec[i].x();
            cdata[ j + 1 ] = ( float )drawObj->m_WireColorVec[i].y();
            cdata[ j + 2 ] = ( float )drawObj->m_WireColorVec[i].z();

            cdata[ j + 3 ] = ( float )drawObj->m_WireAlphaVec[i];
        }

        destObj->emptyLineCBuffer();
        destObj->appendLineCBuffer( cdata.data(), sizeof( float ) * cdata.size());
        destObj->enableLineCBuffer( true );
    }
    else
    {
        destObj->emptyLineCBuffer();
        destObj->enableLineCBuffer( false );
    }

    if ( drawObj->m_FaceColorVec.size() == drawObj->m_PntVec.size() )
    {
        std::vector<float> cdata( n * 4, 0.0f );

        for ( int i = 0; i < ( int )drawObj->m_FaceColorVec.size(); i++ )
        {
            const int j = i * 4;
            cdata[ j + 0 ] = ( float )drawObj->m_FaceColorVec[i].x();
            cdata[ j + 1 ] = ( float )drawObj->m_FaceColorVec[i].y();
            cdata[ j + 2 ] = ( float )drawObj->m_FaceColorVec[i].z();

            cdata[ j + 3 ] = ( float )drawObj->m_FaceAlphaVec[i];
        }

        destObj->emptyMeshCBuffer();
        destObj->appendMeshCBuffer( cdata.data(), sizeof( float ) * cdata.size());
        destObj->enableMeshCBuffer( true );
        destObj->setVertexColorFlag( drawObj->m_VertexColorFlag );
    }
    else
    {
        destObj->emptyMeshCBuffer();
        destObj->enableMeshCBuffer( false );
        destObj->setVertexColorFlag( false );
    }
}

// Tri and Quad data is loaded identically.  Differences are actually based on
// the way the data is stored in the drawObj (vector or mesh) and whether
// it goes into a vertex buffer or a vertex and edge buffer.
// In addition, the amount of data -- vert, vert & norm, or vert, norm & texture.
void VspGlWindow::_loadQuadsData( Renderable * destObj, DrawObj * drawObj )
{
    _loadTrisData( destObj, drawObj );
}

void VspGlWindow::_loadMarkData( Renderable * destObj, DrawObj * drawObj )
{
    unsigned int n = drawObj->m_PntVec.size();
    std::vector<float> data( n * 8, 0.0f );

    for ( int i = 0; i < n; i++ )
    {
        const int j = i * 8;
        data[ j + 0 ] = ( float )drawObj->m_PntVec[i].x();
        data[ j + 1 ] = ( float )drawObj->m_PntVec[i].y();
        data[ j + 2 ] = ( float )drawObj->m_PntVec[i].z();
    }
    destObj->emptyVBuffer();
    destObj->appendVBuffer(data.data(), sizeof( float ) * data.size());
}

void VspGlWindow::_setLighting( DrawObj * drawObj )
{
    if( drawObj->m_Type != DrawObj::VSP_SETTING )
        return;

    // Currently only support up to eight light sources.
    assert( drawObj->m_LightingInfos.size() <= 8 );
    if( drawObj->m_LightingInfos.size() > 8 )
        return;

    DrawObj::LightSourceInfo lInfo;

    LightSource * lSource;

    Lighting * lights = m_GEngine->getScene()->getLights();

    for( unsigned int i = 0; i < (int)drawObj->m_LightingInfos.size(); i++ )
    {
        lInfo = drawObj->m_LightingInfos[i];

        lSource = lights->getLightSource( i );

        lSource->position( lInfo.X, lInfo.Y, lInfo.Z, 0.0f );
        lSource->ambient( lInfo.Amb, lInfo.Amb, lInfo.Amb, 1.0f );
        lSource->diffuse( lInfo.Diff, lInfo.Diff, lInfo.Diff, 1.0f );
        lSource->specular( lInfo.Spec, lInfo.Spec, lInfo.Spec, 1.0f );

        if( lInfo.Active )
            lSource->enable();
        else
            lSource->disable();
    }
}

void VspGlWindow::_setClipping( DrawObj * drawObj )
{
    if( drawObj->m_Type != DrawObj::VSP_CLIP )
        return;

    Clipping * clip = m_GEngine->getScene()->GetClipping();

    for( int i = 0; i < 6; i++ )
    {
        ClipPlane * cplane = clip->getPlane( i );

        if( drawObj->m_ClipFlag[ i ] )
        {
            cplane->enable();
        }
        else
        {
            cplane->disable();
        }

        vec3d n(0, 0, 0);
        vec3d p(0, 0, 0);

        int jaxis = i / 2;  // Integer division implies floor.

        if( i % 2 == 0 )
        {
            n.v[ jaxis ] = -1.0;
        }
        else
        {
            n.v[ jaxis ] = 1.0;
        }

        p.v[ jaxis ] = drawObj->m_ClipLoc[ i ];

        double e[4] = {n.v[0], n.v[1], n.v[2], -dot( n, p )};

        cplane->setEqn( e );
    }
}

void VspGlWindow::OnPush( int x, int y )
{
    VSPGraphic::Display * display = m_GEngine->getDisplay();
    int ivp = display->selectViewport( x, y );
    Vehicle* vPtr = VehicleMgr.GetVehicle();

    if ( ivp != m_prevViewport )
    {
        UpdateAllViewParms();
        m_prevViewport = ivp;
    }

    if( Fl::event_button1() && Fl::event_button3() )
    {
        // LB + RB 
        m_prevLBRB = glm::vec2( x, y );
    }
    else if( Fl::event_button1() )
    {
        bool alltrue = false;

        if( Fl::event_shift() || vPtr->m_SelectBoxFlag() )
        {
            FitModelScreen * fitModelScreen = dynamic_cast< FitModelScreen* >
                ( m_ScreenMgr->GetScreen( vsp::VSP_FIT_MODEL_SCREEN ) );
            if ( fitModelScreen )
            {
                if (fitModelScreen->IsShown() )
                {
                    m_startShiftLB = glm::vec2( x, y );
                    display->getLayoutMgr()->setStartXY( x, y );
                    alltrue = true;
                }
            }
        }

        if( alltrue )
        {
        }
        else if( m_prevKey == 'z' )
        {
            // z
            m_prevZLB = glm::vec2( x, y );
        }
        else if( Fl::event_alt() )
        {
            // Alt + LB
            m_prevAltLB = glm::vec2( x, y );
        }
        else if( Fl::event_ctrl() )
        {
            // Ctrl + LB
            m_prevCtrlLB = glm::vec2( x, y );
        }
        else if( Fl::event_state( FL_META ) )
        {
            // Meta + LB
            m_prevMetaLB = glm::vec2( x, y );
        }
        else
        {
            // LB
            glm::vec3 mouseInWorld = glm::vec3( fNAN );

            // Getting mouse location in world space.  This is for selectLocation().
            Viewport * vp = m_GEngine->getDisplay()->getViewport();
            if( vp )
            {
                mouseInWorld = vp->screenToWorld( glm::vec2( m_mouse_x, m_mouse_y ) );
            }

            // Select highlighted.
            if( m_GEngine->getScene()->selectHighlight() )
            {
                Selectable * selected = m_GEngine->getScene()->getLastSelected();
                _sendFeedback( selected );
            }
            // Select location in world.
            else if( !glm::any(glm::isnan(mouseInWorld)) &&
                m_GEngine->getScene()->selectLocation( mouseInWorld.x, mouseInWorld.y, mouseInWorld.z ) )
            {
                Selectable * selected = m_GEngine->getScene()->getLastSelected();
                _sendFeedback( selected );
            }
            // Rotation.
            else
            {
                m_prevLB = glm::vec2( x, y );
            }
        }
    }
    else if( Fl::event_button2() )
    {
        // MB
        m_prevMB = glm::vec2( x, y );
    }
    else if( Fl::event_button3() )
    {
        // RB
        m_prevRB = glm::vec2( x, y );
    }
    redraw();
}

void VspGlWindow::OnDrag( int x, int y )
{
    VSPGraphic::Display * display = m_GEngine->getDisplay();
    Vehicle* vPtr = VehicleMgr.GetVehicle();

    if( Fl::event_button1() && Fl::event_button3() )
    {
        // LB + RB 
        if( !glm::any(glm::isnan(m_prevLBRB)) )
        {
            display->zoom( ( int )m_prevLBRB.x, ( int )m_prevLBRB.y, x, y );
            UpdateZoomParms();
        }
        m_prevLBRB = glm::vec2( x, y );
    }
    else if( Fl::event_button1() )
    {
        bool alltrue = false;
        if( Fl::event_shift() || vPtr->m_SelectBoxFlag() )
        {
            FitModelScreen * fitModelScreen = dynamic_cast< FitModelScreen* >
                ( m_ScreenMgr->GetScreen( vsp::VSP_FIT_MODEL_SCREEN ) );
            if ( fitModelScreen )
            {
                if (fitModelScreen->IsShown() )
                {
                    if( glm::any(glm::isnan(m_startShiftLB)) )
                    {
                        m_startShiftLB = glm::vec2( x, y );
                        display->getLayoutMgr()->setStartXY( x, y );
                    }
                    alltrue = true;
                }
            }
        }

        if( alltrue )
        {
        }
        else if( m_prevKey == 'z' )
        {
            // z
            if( !glm::any(glm::isnan(m_prevZLB)) )
            {
                display->rotateZ( ( int )m_prevZLB.x, ( int )m_prevZLB.y, x, y );
                UpdateRotationParms();
                EndSnapOrtho();
                // Z was held to roll the view, not to switch wireframe: undo the switch.
                if ( !m_ZRevert.empty() )
                {
                    Vehicle * veh = VehicleMgr.GetVehicle();
                    for ( const auto & r : m_ZRevert )
                    {
                        Geom * g = veh ? veh->FindGeom( r.first ) : nullptr;
                        if ( g )
                        {
                            g->m_GuiDraw.SetDrawType( r.second );
                        }
                    }
                    m_ZRevert.clear();
                    m_ScreenMgr->SetUpdateFlag( true );
                }
            }
            m_prevZLB = glm::vec2( x, y );
        }
        else if( Fl::event_alt() )
        {
            // Alt + LB
            if( !glm::any(glm::isnan(m_prevAltLB)) )
            {
                display->pan( ( int )m_prevAltLB.x, ( int )m_prevAltLB.y, x, y );
                UpdatePanParms();
            }
            m_prevAltLB = glm::vec2( x, y );
        }
        else if( Fl::event_ctrl() )
        {
            // Ctrl + LB
            if( !glm::any(glm::isnan(m_prevCtrlLB)) )
            {
                display->zoom( ( int )m_prevCtrlLB.x, ( int )m_prevCtrlLB.y, x, y );
                UpdateZoomParms();
            }
            m_prevCtrlLB = glm::vec2( x, y );
        }
        else if( Fl::event_state( FL_META ) )
        {
            if( !glm::any(glm::isnan(m_prevMetaLB)) )
            {
                display->zoom( ( int )m_prevMetaLB.x, ( int )m_prevMetaLB.y, x, y );
                UpdateZoomParms();
            }
            m_prevMetaLB = glm::vec2( x, y );
        }
        else if ( !m_noRotate )
        {
            // LB
            if( !glm::any(glm::isnan(m_prevLB)) )
            {
                display->rotate( ( int )m_prevLB.x, ( int )m_prevLB.y, x, y );
                UpdateRotationParms();
                EndSnapOrtho();
            }
            m_prevLB = glm::vec2( x, y );
        }
    }
    else if( Fl::event_button2() )
    {
        // MB
        if( !glm::any(glm::isnan(m_prevMB)) )
        {
            display->zoom( ( int )m_prevMB.x, ( int )m_prevMB.y, x, y );
            UpdateZoomParms();
        }
        m_prevMB = glm::vec2( x, y );
    }
    else if( Fl::event_button3() )
    {
        // RB
        if( !glm::any(glm::isnan(m_prevRB)) )
        {
            display->pan( ( int )m_prevRB.x, ( int )m_prevRB.y, x, y );
            UpdatePanParms();
        }
        m_prevRB = glm::vec2( x, y );
    }
    redraw();
}

void VspGlWindow::OnRelease( int x, int y )
{
    VSPGraphic::Display * display = m_GEngine->getDisplay();
    Vehicle* vPtr = VehicleMgr.GetVehicle();

    // Reset buttons positions.
    switch( Fl::event_button() )
    {
    case FL_LEFT_MOUSE:
        m_prevLB = m_prevAltLB = m_prevCtrlLB = m_prevZLB = glm::vec2( fNAN );
        m_prevMetaLB = glm::vec2( fNAN );
        if( Fl::event_shift() || vPtr->m_SelectBoxFlag() )
        {
            FitModelScreen * fitModelScreen = dynamic_cast< FitModelScreen* >
                ( m_ScreenMgr->GetScreen( vsp::VSP_FIT_MODEL_SCREEN ) );
            if ( fitModelScreen )
            {
                if (fitModelScreen->IsShown() )
                {

                    if( m_GEngine->getScene()->selectBox() )
                    {
                        _sendFeedback( m_GEngine->getScene()->getSelected() );
                    }
                }
            }
        }
        m_startShiftLB = glm::vec2( fNAN );
        display->getLayoutMgr()->setStartXY( -1, -1 );

        break;

    case FL_RIGHT_MOUSE:
        m_prevRB = glm::vec2( fNAN );
        break;

    case FL_MIDDLE_MOUSE:
        m_prevMB = glm::vec2( fNAN );
        break;
    }
    redraw();
}

int VspGlWindow::OnKeyup( int x, int y )
{
    VSPGraphic::Display * display = m_GEngine->getDisplay();
    int handled = 0;

    unsigned key = Fl::event_key();
    key = tolower(key);  // not utf-8 safe, but oh well...

    m_prevKey = '\0';

    switch( key )
    {
    case FL_Escape:
    {
        Vehicle* vPtr = VehicleMgr.GetVehicle();
        if ( vPtr )
        {
            vector< string > none;
            vPtr->SetActiveGeomVec( none );
            m_ScreenMgr->SetUpdateFlag( true );
        }
        handled = 1;
        break;
    }
    case FL_F+1:
        if( Fl::event_state( FL_SHIFT ) )
        {
            display->save( 0 );
        }
        else
        {
            display->load( 0 );

            //===== Update Adjust View Screen with new values =====//
            UpdateAllViewParms();

        }
        handled = 1;
        break;

    case FL_F+2:
        if( Fl::event_state( FL_SHIFT ) )
        {
            display->save( 1 );
        }
        else
        {
            display->load( 1 );

            UpdateAllViewParms();
        }
        handled = 1;
        break;

    case FL_F+3:
        if( Fl::event_state( FL_SHIFT ) )
        {
            display->save( 2 );
        }
        else
        {
            display->load( 2 );

            UpdateAllViewParms();
        }
        handled = 1;
        break;

    case FL_F+4:
        if( Fl::event_state( FL_SHIFT ) )
        {
            display->save( 3 );
        }
        else
        {
            display->load( 3 );

            //===== Update Adjust View Screen with new values =====//
            UpdateAllViewParms();
        }
        handled = 1;
        break;

    case FL_Alt_L:
    case FL_Alt_R:

        // If mouse still pressed, start perform mouse action.
        if( Fl::event_button1() )
        {
            m_prevLB = glm::vec2( x, y );

            // Stop Alt+B control.
            m_prevAltLB = glm::vec2( fNAN );
        }
        if( Fl::event_button2() )
        {
            m_prevMB = glm::vec2( x, y );
        }
        if( Fl::event_button3() )
        {
            m_prevRB = glm::vec2( x, y );
        }
        break;

    case FL_Control_L:
    case FL_Control_R:

        // If mouse still pressed, start perform mouse action.
        if( Fl::event_button1() )
        {
            m_prevLB = glm::vec2( x, y );

            // Stop Alt+B control.
            m_prevCtrlLB = glm::vec2( fNAN );
        }
        if( Fl::event_button2() )
        {
            m_prevMB = glm::vec2( x, y );
        }
        if( Fl::event_button3() )
        {
            m_prevRB = glm::vec2( x, y );
        }
        break;

    case FL_Meta_L:
    case FL_Meta_R:

        // If mouse still pressed, start perform mouse action.
        if( Fl::event_button1() )
        {
            m_prevLB = glm::vec2( x, y );

            // Stop Alt+B control.
            m_prevCtrlLB = glm::vec2( fNAN );
        }
        if( Fl::event_button2() )
        {
            m_prevMB = glm::vec2( x, y );
        }
        if( Fl::event_button3() )
        {
            m_prevRB = glm::vec2( x, y );
        }
        break;

    case int('z'):
        m_prevLB = glm::vec2( x, y );
        m_prevMB = glm::vec2( x, y );
        m_prevRB = glm::vec2( x, y );
        m_prevCtrlLB = glm::vec2( x, y );
        m_prevAltLB = glm::vec2( x, y );
        m_prevZLB = glm::vec2( fNAN );
        break;
    }
    redraw();

    return handled;
}

int VspGlWindow::OnKeydown()
{
    int handled = 0;

    unsigned key = Fl::event_key();
    key = tolower(key);  // not utf-8 safe, but oh well...

    switch( key )
    {
        default:
            m_prevKey = key;
            break;
    }
    redraw();

    return handled;
}

//==== TitanVSP: move gizmo ====//

namespace
{
// Blender's gizmo axis colours.
const float GIZMO_COLOR[3][3] = { { 1.0f, 0.2f, 0.32f }, { 0.54f, 0.86f, 0.0f }, { 0.16f, 0.56f, 1.0f } };

// The components the gizmo moves: the selected ones whose location can be set.  A Fillet, for
// one, is built in place and has its location greyed out.
vector< Geom* > GizmoGeoms()
{
    vector< Geom* > out;
    Vehicle* veh = VehicleMgr.GetVehicle();
    if ( !veh )
    {
        return out;
    }
    for ( Geom* g : veh->GetActiveGeomPtrVec() )
    {
        if ( !g )
        {
            continue;
        }
        bool rel = g->m_AbsRelFlag() == vsp::REL;
        Parm & loc = rel ? g->m_XRelLoc : g->m_XLoc;
        if ( loc.GetActiveFlag() )
        {
            out.push_back( g );
        }
    }
    return out;
}

// Distance from p to the segment a-b.
float SegDist( const glm::vec2 & p, const glm::vec2 & a, const glm::vec2 & b )
{
    glm::vec2 ab = b - a;
    float len2 = glm::dot( ab, ab );
    float t = len2 > 0.0f ? glm::clamp( glm::dot( p - a, ab ) / len2, 0.0f, 1.0f ) : 0.0f;
    return glm::length( p - ( a + t * ab ) );
}
}

// In the main window, with something movable selected, and not while picking points or parts.
bool VspGlWindow::GizmoShown()
{
    if ( m_LinkedScreen != DrawObj::VSP_MAIN_SCREEN || !m_GEngine || m_GEngine->getScene()->isPickingEnabled() )
    {
        return false;
    }
    return !GizmoGeoms().empty();
}

// Where the gizmo sits on screen: the middle of the selected components' origins, projected
// through the selected viewport's camera as the model is drawn.
bool VspGlWindow::GizmoGetFrame( GizmoFrame & f )
{
    vector< Geom* > geoms = GizmoGeoms();
    if ( geoms.empty() )
    {
        return false;
    }

    VSPGraphic::Display * display = m_GEngine->getDisplay();
    VSPGraphic::Viewport * vp = display ? display->getViewport() : nullptr;
    VSPGraphic::Camera * cam = display ? display->getCamera() : nullptr;
    if ( !vp || !cam || vp->width() <= 0 || vp->height() <= 0 )
    {
        return false;
    }

    vec3d o;
    for ( Geom* g : geoms )
    {
        o = o + vec3d( g->m_XLoc(), g->m_YLoc(), g->m_ZLoc() );
    }
    o = o * ( 1.0 / geoms.size() );

    glm::mat4 mv = cam->getModelViewMatrix();
    glm::mat4 pr = cam->getProjectionMatrix();
    glm::vec4 view( vp->x(), vp->y(), vp->width(), vp->height() );

    glm::vec3 wo( o.x(), o.y(), o.z() );
    glm::vec3 so = glm::project( wo, mv, pr, view );
    if ( so.z < 0.0f || so.z > 1.0f )
    {
        return false;
    }
    f.m_Origin = glm::vec2( so.x, so.y );

    // A step along each axis small next to the view, so perspective doesn't bend it.
    float step = 1.0f;
    {
        glm::vec3 a = glm::unProject( glm::vec3( so.x, so.y, so.z ), mv, pr, view );
        glm::vec3 b = glm::unProject( glm::vec3( so.x + 50.0f, so.y, so.z ), mv, pr, view );
        float d = glm::length( b - a );
        if ( d > 0.0f && std::isfinite( d ) )
        {
            step = d;
        }
    }

    float maxlen = 0.0f;
    float len[3];
    for ( int i = 0; i < 3; i++ )
    {
        glm::vec3 axis( 0.0f );
        axis[i] = step;
        glm::vec3 se = glm::project( wo + axis, mv, pr, view );
        glm::vec2 d( se.x - so.x, se.y - so.y );
        len[i] = glm::length( d );
        f.m_Dir[i] = len[i] > 0.0f ? d / len[i] : glm::vec2( 0.0f );
        f.m_PxPerUnit[i] = len[i] / step;
        maxlen = std::max( maxlen, len[i] );
    }
    for ( int i = 0; i < 3; i++ )
    {
        f.m_Visible[i] = maxlen > 0.0f && len[i] > 0.2f * maxlen;
    }
    return true;
}

// The axis whose arrow is under ( x, y ), or -1.
int VspGlWindow::GizmoHit( int x, int y )
{
    GizmoFrame f;
    if ( !GizmoGetFrame( f ) )
    {
        return -1;
    }

    float ppu = pixels_per_unit();
    glm::vec2 p( x, y );
    int best = -1;
    float bestd = 9.0f * ppu;
    for ( int i = 0; i < 3; i++ )
    {
        if ( !f.m_Visible[i] )
        {
            continue;
        }
        glm::vec2 a = f.m_Origin + f.m_Dir[i] * ( 14.0f * ppu );
        glm::vec2 b = f.m_Origin + f.m_Dir[i] * ( 116.0f * ppu );
        float d = SegDist( p, a, b );
        if ( d < bestd )
        {
            bestd = d;
            best = i;
        }
    }
    return best;
}

// A left click on an arrow starts a move along it; anything else is left to the view.
bool VspGlWindow::GizmoPush( int x, int y )
{
    if ( Fl::event_button() != FL_LEFT_MOUSE || Fl::event_state( FL_SHIFT | FL_CTRL | FL_ALT | FL_META ) ||
         !GizmoShown() )
    {
        return false;
    }

    int axis = GizmoHit( x, y );
    GizmoFrame f;
    if ( axis < 0 || !GizmoGetFrame( f ) || f.m_PxPerUnit[axis] <= 0.0f )
    {
        return false;
    }

    m_GizmoAxis = axis;
    m_GizmoHover = axis;
    m_GizmoStart = glm::vec2( x, y );
    m_GizmoDir = f.m_Dir[axis];
    m_GizmoPx = f.m_PxPerUnit[axis];
    m_GizmoSteps = 0;
    m_GizmoUndoSaved = false;

    m_GizmoGeoms.clear();
    m_GizmoRel.clear();
    m_GizmoStartLoc.clear();
    m_GizmoAttachInv.clear();
    for ( Geom* g : GizmoGeoms() )
    {
        bool rel = g->m_AbsRelFlag() == vsp::REL;
        m_GizmoGeoms.push_back( g->GetID() );
        m_GizmoRel.push_back( rel );
        if ( rel )
        {
            m_GizmoStartLoc.push_back( vec3d( g->m_XRelLoc(), g->m_YRelLoc(), g->m_ZRelLoc() ) );
        }
        else
        {
            m_GizmoStartLoc.push_back( vec3d( g->m_XLoc(), g->m_YLoc(), g->m_ZLoc() ) );
        }
        Matrix4d inv = g->getAttachMatrix();
        inv.affineInverse();
        m_GizmoAttachInv.push_back( inv );
    }

    redraw();
    return true;
}

// Moves the components along the axis by the whole number of units the cursor has travelled
// along it on screen.  A component placed relative to its parent moves by the same amount in the
// model: the move is turned back through the parent's attachment rotation into its own Rel
// Location.
void VspGlWindow::GizmoDrag( int x, int y )
{
    float along = glm::dot( glm::vec2( x, y ) - m_GizmoStart, m_GizmoDir );
    int steps = ( int )std::lround( along / m_GizmoPx );
    if ( steps == m_GizmoSteps )
    {
        return;
    }
    m_GizmoSteps = steps;

    Vehicle* veh = VehicleMgr.GetVehicle();
    if ( !veh )
    {
        return;
    }

    vec3d move;
    move[ m_GizmoAxis ] = steps;

    // Undo, as for a slider: the move's first step is set as a change of its own, which records
    // where each Parm started; the rest are drag updates, which aren't recorded.  Undo then puts
    // the components back where the drag began.
    bool drag = m_GizmoUndoSaved;
    m_GizmoUndoSaved = true;

    for ( int i = 0; i < ( int )m_GizmoGeoms.size(); i++ )
    {
        Geom* g = veh->FindGeom( m_GizmoGeoms[i] );
        if ( !g )
        {
            continue;
        }

        vec3d loc;
        if ( m_GizmoRel[i] )
        {
            loc = m_GizmoStartLoc[i] + m_GizmoAttachInv[i].xformnorm( move );
            g->m_XRelLoc.SetFromDevice( loc.x(), drag );
            g->m_YRelLoc.SetFromDevice( loc.y(), drag );
            g->m_ZRelLoc.SetFromDevice( loc.z(), drag );
        }
        else
        {
            loc = m_GizmoStartLoc[i] + move;
            g->m_XLoc.SetFromDevice( loc.x(), drag );
            g->m_YLoc.SetFromDevice( loc.y(), drag );
            g->m_ZLoc.SetFromDevice( loc.z(), drag );
        }
    }

    m_ScreenMgr->SetUpdateFlag( true );
    redraw();
}

void VspGlWindow::GizmoRelease()
{
    m_GizmoAxis = -1;
    m_GizmoGeoms.clear();
    m_GizmoRel.clear();
    m_GizmoStartLoc.clear();
    m_GizmoAttachInv.clear();
    m_ScreenMgr->SetUpdateFlag( true );
    redraw();
}

// The arrows, over everything, at a fixed size on screen.  The one under the cursor, or being
// dragged, is drawn brighter and wider.
void VspGlWindow::GizmoDraw()
{
    GizmoFrame f;
    if ( !GizmoShown() || !GizmoGetFrame( f ) )
    {
        return;
    }

    float ppu = pixels_per_unit();
    const float gap = 14.0f * ppu, len = 100.0f * ppu, head = 16.0f * ppu, half = 6.0f * ppu;

    GLint program = 0;
    glGetIntegerv( GL_CURRENT_PROGRAM, &program );
    glUseProgram( 0 );

    glPushAttrib( GL_ALL_ATTRIB_BITS );
    glDisable( GL_SCISSOR_TEST );
    glDisable( GL_DEPTH_TEST );
    glDisable( GL_LIGHTING );
    glDisable( GL_TEXTURE_2D );
    glDisable( GL_CULL_FACE );
    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glEnable( GL_LINE_SMOOTH );

    glViewport( 0, 0, pixel_w(), pixel_h() );
    glMatrixMode( GL_PROJECTION );
    glPushMatrix();
    glLoadIdentity();
    glOrtho( 0, pixel_w(), 0, pixel_h(), -1, 1 );
    glMatrixMode( GL_MODELVIEW );
    glPushMatrix();
    glLoadIdentity();

    // Back to front: the axes pointing away from the viewer first does not matter on screen, so
    // draw the hovered one last to keep it on top.
    int order[3] = { 0, 1, 2 };
    int active = m_GizmoAxis >= 0 ? m_GizmoAxis : m_GizmoHover;
    if ( active >= 0 )
    {
        std::swap( order[ active ], order[2] );
    }

    for ( int k = 0; k < 3; k++ )
    {
        int i = order[k];
        if ( !f.m_Visible[i] )
        {
            continue;
        }
        bool hot = i == active;
        float c[3] = { GIZMO_COLOR[i][0], GIZMO_COLOR[i][1], GIZMO_COLOR[i][2] };
        if ( hot )
        {
            for ( float & ch : c )
            {
                ch = ch + ( 1.0f - ch ) * 0.45f;
            }
        }
        glColor4f( c[0], c[1], c[2], 1.0f );

        glm::vec2 d = f.m_Dir[i];
        glm::vec2 n( -d.y, d.x );
        glm::vec2 a = f.m_Origin + d * gap;
        glm::vec2 b = f.m_Origin + d * len;
        glm::vec2 tip = f.m_Origin + d * ( len + head );

        glLineWidth( ( hot ? 4.0f : 3.0f ) * ppu );
        glBegin( GL_LINES );
        glVertex2f( a.x, a.y );
        glVertex2f( b.x, b.y );
        glEnd();

        glBegin( GL_TRIANGLES );
        glVertex2f( tip.x, tip.y );
        glVertex2f( b.x + n.x * half, b.y + n.y * half );
        glVertex2f( b.x - n.x * half, b.y - n.y * half );
        glEnd();
    }

    // A small white ring at the origin.
    glColor4f( 1.0f, 1.0f, 1.0f, 0.9f );
    glLineWidth( 2.0f * ppu );
    glBegin( GL_LINE_LOOP );
    for ( int k = 0; k < 24; k++ )
    {
        float t = 2.0f * 3.14159265f * k / 24.0f;
        glVertex2f( f.m_Origin.x + 5.0f * ppu * std::cos( t ), f.m_Origin.y + 5.0f * ppu * std::sin( t ) );
    }
    glEnd();

    glMatrixMode( GL_PROJECTION );
    glPopMatrix();
    glMatrixMode( GL_MODELVIEW );
    glPopMatrix();
    glPopAttrib();

    glUseProgram( program );
}

//==== TitanVSP: animated view changes ====//

void VspGlWindow::AnimateView( VSPGraphic::Common::VSPenum type, const BndBox * fit, bool zoom, bool animate, std::function< void() > done )
{
    VSPGraphic::Display * display = m_GEngine->getDisplay();
    VSPGraphic::Viewport * vp = display ? display->getViewport() : nullptr;
    VSPGraphic::Camera * cam = display ? display->getCamera() : nullptr;

    // Start from wherever the camera is, mid-way through another change or not.
    StopViewAnim();
    if ( !vp || !cam )
    {
        if ( display )
        {
            display->changeView( type );
        }
        UpdateAllViewParms();
        if ( done )
        {
            done();
        }
        return;
    }

    // Where the camera ends up: the view, then the box centred and framed in it.
    VSPGraphic::Camera::Pose from = cam->getPose();
    cam->changeView( type );
    if ( fit && vp->width() > 0 && vp->height() > 0 )
    {
        const BndBox & bb = *fit;
        vec3d c = bb.GetCenter();
        cam->setCOR( -c.x(), -c.y(), -c.z() );
        cam->relativePan( 0.0f, 0.0f );

        // The box's corners in the view: half its width and height on screen, in model units.
        glm::mat4 mv = cam->getModelViewMatrix();
        float hx = 0.0f, hy = 0.0f;
        for ( const vec3d & p : bb.GetCornerPnts() )
        {
            glm::vec4 q = mv * glm::vec4( p.x(), p.y(), p.z(), 1.0f );
            hx = std::max( hx, std::fabs( q.x ) );
            hy = std::max( hy, std::fabs( q.y ) );
        }
        // Zoom is model units per pixel; leave a margin round the model.
        float z = 1.15f * std::max( 2.0f * hx / vp->width(), 2.0f * hy / vp->height() );
        if ( zoom && z > 0.0f && std::isfinite( z ) )
        {
            cam->relativeZoom( z );
        }
    }
    VSPGraphic::Camera::Pose to = cam->getPose();
    if ( !animate )
    {
        UpdateAllViewParms();
        redraw();
        if ( done )
        {
            done();
        }
        return;
    }
    cam->setPose( from );
    AnimateToPose( to, 0.35, done );
}

// Moves the selected viewport's camera from where it is to 'to' over duration seconds.
void VspGlWindow::AnimateToPose( const VSPGraphic::Camera::Pose & to, double duration, std::function< void() > done )
{
    StopViewAnim();
    VSPGraphic::Camera * cam = m_GEngine->getDisplay() ? m_GEngine->getDisplay()->getCamera() : nullptr;
    if ( !cam )
    {
        if ( done )
        {
            done();
        }
        return;
    }
    m_ViewAnimFrom = cam->getPose();
    m_ViewAnimTo = to;
    m_ViewAnimCam = cam;
    m_ViewAnimDone = done;
    m_ViewAnimDuration = duration;
    m_ViewAnimStart = std::chrono::steady_clock::now();
    m_ViewAnim = true;
    Fl::add_timeout( 1.0 / 60.0, ViewAnimCB, this );
}

// Leaves the camera where it is.
void VspGlWindow::StopViewAnim()
{
    if ( m_ViewAnim )
    {
        Fl::remove_timeout( ViewAnimCB, this );
        m_ViewAnim = false;
        m_ViewAnimDone = nullptr;
        UpdateAllViewParms();
    }
}

void VspGlWindow::ViewAnimCB( void * data )
{
    VspGlWindow * win = static_cast< VspGlWindow * >( data );
    if ( !win || !win->m_ViewAnim )
    {
        return;
    }

    // The layout or the selected viewport changed: let it be.
    VSPGraphic::Camera * cam = win->m_GEngine->getDisplay()->getCamera();
    if ( cam != win->m_ViewAnimCam )
    {
        win->m_ViewAnim = false;
        win->m_ViewAnimDone = nullptr;
        return;
    }

    double t = std::chrono::duration< double >( std::chrono::steady_clock::now() - win->m_ViewAnimStart ).count() / win->m_ViewAnimDuration;
    if ( t >= 1.0 )
    {
        cam->setPose( win->m_ViewAnimTo );
        win->m_ViewAnim = false;
        std::function< void() > done = win->m_ViewAnimDone;
        win->m_ViewAnimDone = nullptr;
        win->UpdateAllViewParms();
        win->redraw();
        if ( done )
        {
            done();
        }
        return;
    }

    // Ease in and out.
    float e = (float)( t * t * t * ( t * ( 6.0 * t - 15.0 ) + 10.0 ) );
    cam->setPose( VSPGraphic::Camera::blendPose( win->m_ViewAnimFrom, win->m_ViewAnimTo, e ) );
    win->redraw();
    Fl::repeat_timeout( 1.0 / 60.0, ViewAnimCB, data );
}

//==== TitanVSP: Blender's keys for the selection and the view ====//

namespace
{
ManageGeomScreen * GeomBrowser( ScreenMgr * mgr )
{
    return dynamic_cast< ManageGeomScreen * >( mgr->GetScreen( vsp::VSP_MANAGE_GEOM_SCREEN ) );
}
}

bool VspGlWindow::BlenderKey()
{
    if ( m_LinkedScreen != DrawObj::VSP_MAIN_SCREEN || m_XfMode != XF_NONE || !m_GEngine )
    {
        return false;
    }

    int key = Fl::event_key();
    if ( m_KeysOpen && key == FL_Escape )
    {
        m_KeysOpen = false;
        m_XfSwallowKeyUp = true;    // Esc's key-up would clear the selection.
        redraw();
        return true;
    }
    if ( Fl::event_state( FL_CTRL | FL_META ) || m_GEngine->getScene()->isPickingEnabled() )
    {
        return false;
    }
    bool shift = Fl::event_state( FL_SHIFT ) != 0;
    bool alt = Fl::event_state( FL_ALT ) != 0;
    ManageGeomScreen * browser = GeomBrowser( m_ScreenMgr );
    if ( !browser )
    {
        return false;
    }

    switch ( key )
    {
    case 'a':
        if ( shift )
        {
            return false;
        }
        if ( alt )
        {
            browser->SetSelection( vector< string >() );
        }
        else
        {
            browser->KeySelectAll();
        }
        break;
    case 'h':
        if ( alt && shift )
        {
            return false;
        }
        if ( alt )
        {
            browser->KeyShowAll();
        }
        else
        {
            browser->KeyHide( shift );
        }
        break;
    case 'z':
    {
        if ( alt )
        {
            return false;
        }
        // Held down, Z repeats; only a fresh press switches.  Z also rolls the view while
        // dragging, so it's noted as the key held (OnKeydown isn't reached).
        m_prevKey = 'z';
        auto now = std::chrono::steady_clock::now();
        bool repeat = m_ZHeld && std::chrono::duration< double >( now - m_ZLast ).count() < 0.6;
        m_ZLast = now;
        m_ZHeld = true;
        if ( !repeat )
        {
            ToggleWire( !shift );
        }
        return true;
    }
    case 'd':
        if ( !shift || alt )
        {
            return false;
        }
        DuplicateSelection();
        return true;
    case 'x':
    case FL_Delete:
        if ( shift || alt )
        {
            return false;
        }
        DeleteSelection();
        return true;
    default:
        return false;
    }
    m_ScreenMgr->SetUpdateFlag( true );
    return true;
}

// Z: the selection wireframe, or shaded when it's all wireframe already.  Shift+Z: the same for
// every component, judged by the shown ones.  Components drawn as None stay that way.
void VspGlWindow::ToggleWire( bool selected )
{
    Vehicle * veh = VehicleMgr.GetVehicle();
    if ( !veh )
    {
        return;
    }
    vector< Geom * > geoms;
    for ( Geom * g : veh->FindGeomVec( selected ? veh->GetActiveGeomVec() : veh->GetGeomVec() ) )
    {
        if ( g && g->m_GuiDraw.GetDrawType() != vsp::DRAW_TYPE::GEOM_DRAW_NONE )
        {
            geoms.push_back( g );
        }
    }
    if ( geoms.empty() )
    {
        return;
    }
    bool all_wire = true;
    bool any_judged = false;
    for ( Geom * g : geoms )
    {
        if ( !selected && !g->GetSetFlag( vsp::SET_SHOWN ) )
        {
            continue;
        }
        any_judged = true;
        all_wire = all_wire && g->m_GuiDraw.GetDrawType() == vsp::DRAW_TYPE::GEOM_DRAW_WIRE;
    }
    if ( !any_judged )
    {
        for ( Geom * g : geoms )
        {
            all_wire = all_wire && g->m_GuiDraw.GetDrawType() == vsp::DRAW_TYPE::GEOM_DRAW_WIRE;
        }
    }
    m_ZRevert.clear();
    for ( Geom * g : geoms )
    {
        m_ZRevert.push_back( std::make_pair( g->GetID(), g->m_GuiDraw.GetDrawType() ) );
        g->m_GuiDraw.SetDrawType( all_wire ? vsp::DRAW_TYPE::GEOM_DRAW_SHADE : vsp::DRAW_TYPE::GEOM_DRAW_WIRE );
    }
    m_ScreenMgr->SetUpdateFlag( true );
}

// X / Delete: asks first, then deletes as Edit > Delete does (which asks about clones).
void VspGlWindow::DeleteSelection()
{
    Vehicle * veh = VehicleMgr.GetVehicle();
    if ( !veh )
    {
        return;
    }
    int n = ( int )veh->GetActiveGeomVec().size();
    if ( n == 0 )
    {
        return;
    }
    string what = n == 1 ? string( "the selected component" ) : std::to_string( n ) + " selected components";
    if ( fl_choice( "Delete %s?", "Cancel", "Delete", nullptr, what.c_str() ) != 1 )
    {
        return;
    }
    DeleteOrCutActiveGeomVec( veh, false );
    m_ScreenMgr->SetUpdateFlag( true );
}

// Shift+D: copies of the selection in place, selected and following the mouse (G).  Esc leaves
// them where they are, on top of the originals.
void VspGlWindow::DuplicateSelection()
{
    Vehicle * veh = VehicleMgr.GetVehicle();
    ManageGeomScreen * browser = GeomBrowser( m_ScreenMgr );
    if ( !veh || !browser || veh->GetActiveGeomVec().empty() )
    {
        return;
    }
    vector< string > copies = veh->DuplicateGeomVec( veh->GetActiveGeomVec() );
    if ( copies.empty() )
    {
        return;
    }
    browser->SetSelection( copies );
    veh->Update();
    m_ScreenMgr->SetUpdateFlag( true );
    if ( !GizmoGeoms().empty() )
    {
        XfBegin( XF_MOVE );
    }
}

// Blender's numpad: 4 / 6 turn the model about the vertical (Z) axis, 8 / 2 tip the view up or
// down, 15 degrees a step; 9 looks from the other side.  Steps add up even mid-animation.
void VspGlWindow::OrbitStep( int key )
{
    VSPGraphic::Camera * cam = m_GEngine->getDisplay() ? m_GEngine->getDisplay()->getCamera() : nullptr;
    if ( !cam )
    {
        return;
    }
    VSPGraphic::Camera::Pose base = m_ViewAnim ? m_ViewAnimTo : cam->getPose();
    VSPGraphic::Camera::Pose to = base;
    const float step = glm::radians( 15.0f );
    switch ( key )
    {
    case '4':
        to.rot = glm::rotate( base.rot, -step, glm::vec3( 0.0f, 0.0f, 1.0f ) );
        break;
    case '6':
        to.rot = glm::rotate( base.rot, step, glm::vec3( 0.0f, 0.0f, 1.0f ) );
        break;
    case '8':
        to.rot = glm::rotate( glm::mat4( 1.0f ), step, glm::vec3( 1.0f, 0.0f, 0.0f ) ) * base.rot;
        break;
    case '2':
        to.rot = glm::rotate( glm::mat4( 1.0f ), -step, glm::vec3( 1.0f, 0.0f, 0.0f ) ) * base.rot;
        break;
    case '9':
        to.rot = glm::rotate( glm::mat4( 1.0f ), glm::pi< float >(), glm::vec3( 0.0f, 1.0f, 0.0f ) ) * base.rot;
        break;
    default:
        return;
    }
    AnimateToPose( to, key == '9' ? 0.35 : 0.15, nullptr );
    // Turning off an axis view goes back to perspective; the other side of one is still one.
    if ( key != '9' )
    {
        EndSnapOrtho();
    }
}

//==== TitanVSP: the shortcuts popup ====//

namespace
{
struct KeyRow
{
    const char * keys;
    const char * what;
};
struct KeySection
{
    const char * title;
    vector< KeyRow > rows;
};

// The 3D view's keys, Blender's, as the popup lists them.
const vector< KeySection > & KeySections()
{
    static const vector< KeySection > sections = {
        { "Select and show", {
            { "Click", "Select a component" },
            { "Shift / Ctrl + click", "Add to or take from the selection" },
            { "A", "Select all" },
            { "Alt + A", "Deselect all" },
            { "H", "Hide the selection" },
            { "Shift + H", "Hide everything else" },
            { "Alt + H", "Show everything" },
            { "Z", "Selection: wireframe / shaded" },
            { "Shift + Z", "Every component: wireframe / shaded" },
        } },
        { "Move and rotate", {
            { "G", "Move the selection with the mouse" },
            { "R", "Rotate it with the mouse" },
            { "Shift + D", "Duplicate, then move the copies" },
            { "X / Delete", "Delete the selection (asks first)" },
            { "X / Y / Z", "Hold to that axis (again to let go)" },
            { "G  X  10", "Move 10 along X (type the value)" },
            { "R  Z  90", "Rotate 90 degrees about Z" },
            { "Ctrl", "Snap: whole units, 5 degrees" },
            { "Enter / click", "Apply" },
            { "Esc / right-click", "Cancel" },
        } },
        { "View", {
            { "1 / 3 / 7", "Front / left / top (Ctrl: the other side)" },
            { "5", "Perspective / orthographic" },
            { "2 4 6 8", "Orbit in steps" },
            { "9", "Look from the other side" },
            { "F / Home / numpad .", "Fit the model" },
            { "C", "Center the model (zoom kept)" },
            { "F5 - F12", "The View menu's views" },
            { "Left drag", "Orbit" },
            { "Right drag", "Pan" },
            { "Wheel", "Zoom" },
        } },
    };
    return sections;
}

void FillRect( float x, float y, float w, float h )
{
    glBegin( GL_QUADS );
    glVertex2f( x, y );
    glVertex2f( x + w, y );
    glVertex2f( x + w, y + h );
    glVertex2f( x, y + h );
    glEnd();
}

void FrameRect( float x, float y, float w, float h )
{
    glBegin( GL_LINE_LOOP );
    glVertex2f( x + 0.5f, y + 0.5f );
    glVertex2f( x + w - 0.5f, y + 0.5f );
    glVertex2f( x + w - 0.5f, y + h - 0.5f );
    glVertex2f( x + 0.5f, y + h - 0.5f );
    glEnd();
}
}

bool VspGlWindow::KeysIconHit( int x, int y )
{
    return m_LinkedScreen == DrawObj::VSP_MAIN_SCREEN && m_KeysIcon[2] > 0 &&
           x >= m_KeysIcon[0] && x <= m_KeysIcon[0] + m_KeysIcon[2] &&
           y >= m_KeysIcon[1] && y <= m_KeysIcon[1] + m_KeysIcon[3];
}

bool VspGlWindow::KeysPanelHit( int x, int y )
{
    return m_KeysPanel[2] > 0 &&
           x >= m_KeysPanel[0] && x <= m_KeysPanel[0] + m_KeysPanel[2] &&
           y >= m_KeysPanel[1] && y <= m_KeysPanel[1] + m_KeysPanel[3];
}

// The keyboard button in the bottom left corner, and its list when open.  Main window only.
void VspGlWindow::KeysDraw()
{
    if ( m_LinkedScreen != DrawObj::VSP_MAIN_SCREEN )
    {
        return;
    }

    float ppu = pixels_per_unit();

    GLint program = 0;
    glGetIntegerv( GL_CURRENT_PROGRAM, &program );
    glUseProgram( 0 );

    glPushAttrib( GL_ALL_ATTRIB_BITS );
    glDisable( GL_SCISSOR_TEST );
    glDisable( GL_DEPTH_TEST );
    glDisable( GL_LIGHTING );
    glDisable( GL_TEXTURE_2D );
    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

    glViewport( 0, 0, pixel_w(), pixel_h() );
    glMatrixMode( GL_PROJECTION );
    glPushMatrix();
    glLoadIdentity();
    glOrtho( 0, pixel_w(), 0, pixel_h(), -1, 1 );
    glMatrixMode( GL_MODELVIEW );
    glPushMatrix();
    glLoadIdentity();

    //==== The button: a keyboard ====//
    float bx = 14.0f * ppu, by = 12.0f * ppu, bw = 30.0f * ppu, bh = 22.0f * ppu;
    m_KeysIcon[0] = ( int )bx;
    m_KeysIcon[1] = ( int )by;
    m_KeysIcon[2] = ( int )bw;
    m_KeysIcon[3] = ( int )bh;
    bool lit = m_KeysOpen || m_KeysHover;
    if ( lit )
    {
        glColor4f( 1.0f, 1.0f, 1.0f, m_KeysOpen ? 0.16f : 0.09f );
        FillRect( bx, by, bw, bh );
    }
    float a = lit ? 0.95f : 0.6f;
    glColor4f( 0.88f, 0.90f, 0.93f, a );
    glLineWidth( 1.3f * ppu );
    float kx = bx + 5.0f * ppu, ky = by + 4.5f * ppu, kw = bw - 10.0f * ppu, kh = bh - 9.0f * ppu;
    FrameRect( kx, ky, kw, kh );
    // Two rows of keys and a space bar.
    float dot = 1.6f * ppu;
    for ( int r = 0; r < 2; r++ )
    {
        float yy = ky + kh - ( 3.5f + 3.6f * r ) * ppu;
        for ( int c = 0; c < 5; c++ )
        {
            if ( r == 1 && ( c == 0 || c == 4 ) )
            {
                continue;
            }
            float xx = kx + ( 2.6f + c * ( kw / ppu - 5.2f ) / 4.0f ) * ppu;
            FillRect( xx - dot * 0.5f, yy - dot * 0.5f, dot, dot );
        }
    }
    FillRect( kx + kw * 0.3f, ky + 2.0f * ppu, kw * 0.4f, 1.3f * ppu );

    m_KeysPanel[0] = m_KeysPanel[1] = m_KeysPanel[2] = m_KeysPanel[3] = 0;
    if ( m_KeysOpen )
    {
        //==== The list ====//
        const vector< KeySection > & sections = KeySections();
        const float pad = 14.0f * ppu;
        const float row = 19.0f * ppu;
        const float gap = 10.0f * ppu;      // Between a key and its description.
        const float colgap = 26.0f * ppu;   // Between sections laid side by side.

        // Each section's key and description widths.  gl_width measures in screen units; the text
        // is drawn ppu times that in pixels.
        vector< float > keyw( sections.size(), 0.0f ), descw( sections.size(), 0.0f );
        for ( size_t i = 0; i < sections.size(); i++ )
        {
            gl_font( FL_HELVETICA_BOLD, 13 );
            keyw[i] = std::max( keyw[i], ( float )gl_width( sections[i].title ) * ppu );
            for ( const KeyRow & k : sections[i].rows )
            {
                keyw[i] = std::max( keyw[i], ( float )gl_width( k.keys ) * ppu );
            }
            gl_font( FL_HELVETICA, 13 );
            for ( const KeyRow & k : sections[i].rows )
            {
                descw[i] = std::max( descw[i], ( float )gl_width( k.what ) * ppu );
            }
            keyw[i] = std::min( keyw[i], 170.0f * ppu );
        }

        // Side by side when they fit the window, else stacked.
        float wide = 2.0f * pad;
        size_t most = 0;
        for ( size_t i = 0; i < sections.size(); i++ )
        {
            wide += keyw[i] + gap + descw[i] + ( i ? colgap : 0.0f );
            most = std::max( most, sections[i].rows.size() );
        }
        bool side = wide <= pixel_w() - 2.0f * bx;

        float pw, ph;
        if ( side )
        {
            pw = wide;
            ph = 2.0f * pad + row * ( 1.5f + most );
        }
        else
        {
            float kmax = 0.0f, dmax = 0.0f;
            size_t rows = 0;
            for ( size_t i = 0; i < sections.size(); i++ )
            {
                kmax = std::max( kmax, keyw[i] );
                dmax = std::max( dmax, descw[i] );
                rows += sections[i].rows.size();
            }
            pw = 2.0f * pad + kmax + gap + dmax;
            ph = 2.0f * pad + row * ( rows + 1.5f * sections.size() ) - 0.5f * row;
            for ( size_t i = 0; i < sections.size(); i++ )
            {
                keyw[i] = kmax;
            }
        }

        float px = bx;
        float py = by + bh + 8.0f * ppu;
        m_KeysPanel[0] = ( int )px;
        m_KeysPanel[1] = ( int )py;
        m_KeysPanel[2] = ( int )pw;
        m_KeysPanel[3] = ( int )ph;

        glColor4f( 0.115f, 0.13f, 0.15f, 0.95f );
        FillRect( px, py, pw, ph );
        glColor4f( 0.40f, 0.43f, 0.48f, 0.9f );
        glLineWidth( 1.0f );
        FrameRect( px, py, pw, ph );

        float cx = px + pad;
        float top = py + ph - pad;
        float cy = top;
        for ( size_t i = 0; i < sections.size(); i++ )
        {
            if ( side && i )
            {
                cx += keyw[i - 1] + gap + descw[i - 1] + colgap;
                cy = top;
            }
            cy -= row;
            gl_font( FL_HELVETICA_BOLD, 13 );
            glColor4f( 0.50f, 0.72f, 1.0f, 1.0f );
            gl_draw( sections[i].title, cx, cy + 4.0f * ppu );
            cy -= 0.5f * row;
            for ( const KeyRow & k : sections[i].rows )
            {
                cy -= row;
                gl_font( FL_HELVETICA_BOLD, 13 );
                glColor4f( 0.96f, 0.97f, 0.98f, 1.0f );
                gl_draw( k.keys, cx, cy + 5.0f * ppu );
                gl_font( FL_HELVETICA, 13 );
                glColor4f( 0.74f, 0.77f, 0.81f, 1.0f );
                gl_draw( k.what, cx + keyw[i] + gap, cy + 5.0f * ppu );
            }
            if ( !side )
            {
                cy -= 0.5f * row;
            }
        }
    }

    glMatrixMode( GL_PROJECTION );
    glPopMatrix();
    glMatrixMode( GL_MODELVIEW );
    glPopMatrix();
    glPopAttrib();

    glUseProgram( program );
}

//==== TitanVSP: Blender's view keys and auto perspective ====//

bool VspGlWindow::ViewKey()
{
    if ( m_LinkedScreen != DrawObj::VSP_MAIN_SCREEN || m_XfMode != XF_NONE ||
         Fl::event_state( FL_ALT | FL_META | FL_SHIFT ) )
    {
        return false;
    }
    MainVSPScreen* main = dynamic_cast< MainVSPScreen* >( m_ScreenMgr->GetScreen( vsp::VSP_MAIN_SCREEN ) );
    if ( !main )
    {
        return false;
    }

    int key = Fl::event_key();
    bool keypad = key >= FL_KP && key <= FL_KP_Last;
    if ( keypad )
    {
        key -= FL_KP;
    }
    bool ctrl = Fl::event_ctrl() != 0;

    int view = -1;
    switch ( key )
    {
    case '1':
        view = ctrl ? VSPGraphic::Common::VSP_CAM_REAR : VSPGraphic::Common::VSP_CAM_FRONT;
        break;
    case '3':
        view = ctrl ? VSPGraphic::Common::VSP_CAM_RIGHT : VSPGraphic::Common::VSP_CAM_LEFT;     // Nose to the left.
        break;
    case '7':
        view = ctrl ? VSPGraphic::Common::VSP_CAM_BOTTOM : VSPGraphic::Common::VSP_CAM_TOP;
        break;
    case '5':
        // The number row's 5 is the View menu's shortcut; the keypad's is handled here.
        if ( keypad && !ctrl )
        {
            main->TogglePerspective();
            return true;
        }
        return false;
    case '2':
    case '4':
    case '6':
    case '8':
    case '9':
        if ( ctrl )
        {
            return false;
        }
        OrbitStep( key );
        return true;
    case '.':
        if ( !keypad || ctrl )
        {
            return false;
        }
        main->FitViewAnimated( true );
        return true;
    case FL_Home:
        if ( ctrl )
        {
            return false;
        }
        main->FitViewAnimated( true );
        return true;
    default:
        return false;
    }

    main->SetView( view );
    m_ScreenMgr->SetUpdateFlag( true );
    return true;
}

void VspGlWindow::EndSnapOrtho()
{
    if ( m_LinkedScreen != DrawObj::VSP_MAIN_SCREEN )
    {
        return;
    }
    MainVSPScreen* main = dynamic_cast< MainVSPScreen* >( m_ScreenMgr->GetScreen( vsp::VSP_MAIN_SCREEN ) );
    if ( main )
    {
        main->EndSnapOrtho();
    }
}

//==== TitanVSP: keyboard move and rotate ====//

// Handles a key for the move/rotate.  Returns true if it was used.
bool VspGlWindow::XfKey()
{
    if ( m_LinkedScreen != DrawObj::VSP_MAIN_SCREEN || !m_GEngine )
    {
        return false;
    }

    int key = Fl::event_key();
    const char * text = Fl::event_text();
    char ch = ( text && text[0] ) ? text[0] : 0;
    bool mods = Fl::event_state( FL_CTRL | FL_ALT | FL_META ) != 0;

    if ( m_XfMode == XF_NONE )
    {
        if ( mods || m_GizmoAxis >= 0 || m_GEngine->getScene()->isPickingEnabled() || GizmoGeoms().empty() )
        {
            return false;
        }
        if ( key == 'g' )
        {
            XfBegin( XF_MOVE );
            return true;
        }
        if ( key == 'r' )
        {
            XfBegin( XF_ROTATE );
            return true;
        }
        return false;
    }

    if ( key == FL_Escape )
    {
        XfEnd( false );
        m_XfSwallowKeyUp = true;
        return true;
    }
    if ( key == FL_Enter || key == FL_KP_Enter )
    {
        XfEnd( true );
        m_XfSwallowKeyUp = true;
        return true;
    }
    if ( key == 'g' || key == 'r' )
    {
        int mode = key == 'g' ? XF_MOVE : XF_ROTATE;
        if ( mode != m_XfMode )
        {
            XfRestore();
            XfBegin( mode );
        }
        return true;
    }
    if ( key == FL_Control_L || key == FL_Control_R )
    {
        m_XfSnap = true;
        XfApply( false );
        redraw();
        return true;
    }
    if ( ( key == 'x' || key == 'y' || key == 'z' ) && !mods )
    {
        // The same axis again lets go of it.
        int axis = key - 'x';
        XfRestore();
        m_XfAxis = axis == m_XfAxis ? -1 : axis;
        if ( m_XfAxis < 0 )
        {
            m_XfText.clear();
        }
        XfApply( false );
        redraw();
        return true;
    }
    if ( key == FL_BackSpace )
    {
        if ( !m_XfText.empty() )
        {
            m_XfText.pop_back();
            XfApply( false );
            redraw();
        }
        return true;
    }
    // A number: along the held axis, or X for a move and the line of sight's axis for a rotation.
    if ( ch && m_XfAxis < 0 && ( ( ch >= '0' && ch <= '9' ) || ch == '.' || ch == '-' ) )
    {
        XfRestore();
        m_XfAxis = m_XfMode == XF_MOVE ? 0 : m_XfRotAxis;
    }
    if ( m_XfAxis >= 0 && ch )
    {
        if ( ch >= '0' && ch <= '9' )
        {
            m_XfText += ch;
        }
        else if ( ch == '.' && m_XfText.find( '.' ) == string::npos )
        {
            m_XfText += ch;
        }
        else if ( ch == '-' )
        {
            if ( !m_XfText.empty() && m_XfText[0] == '-' )
            {
                m_XfText.erase( 0, 1 );
            }
            else
            {
                m_XfText.insert( 0, 1, '-' );
            }
        }
        XfApply( false );
        redraw();
    }
    // Every other key is swallowed while typing, so it can't trigger a shortcut half way.
    return true;
}

// Starts a move or rotate of the selected components, remembering where each one is.
void VspGlWindow::XfBegin( int mode )
{
    m_XfMode = mode;
    m_XfAxis = -1;
    m_XfText.clear();
    m_XfGeoms.clear();
    m_XfRel.clear();
    m_XfStartLoc.clear();
    m_XfStartRot.clear();
    m_XfAttachInv.clear();

    // The mouse drives it from here.
    m_XfMouse0 = m_XfMouseNow = m_XfMousePrev = m_HoverPos;
    m_XfAngle = 0.0;
    m_XfMoved = false;
    m_XfSnap = Fl::event_state( FL_CTRL ) != 0;
    m_XfFrameOK = GizmoGetFrame( m_XfFrame );

    // A rotation with no axis held turns about the axis nearest the line of sight, the way that
    // matches the mouse going round: anticlockwise on screen is positive about an axis pointing at
    // the viewer.
    m_XfRotAxis = 2;
    VSPGraphic::Camera * cam = m_GEngine->getDisplay() ? m_GEngine->getDisplay()->getCamera() : nullptr;
    if ( cam )
    {
        glm::mat4 mv = cam->getModelViewMatrix();
        float best = -1.0f;
        for ( int k = 0; k < 3; k++ )
        {
            float toward = mv[k][2];    // World axis k's component along the camera's z, toward the viewer.
            m_XfRotSign[k] = toward >= 0.0f ? 1.0 : -1.0;
            if ( std::fabs( toward ) > best )
            {
                best = std::fabs( toward );
                m_XfRotAxis = k;
            }
        }
    }

    for ( Geom * g : GizmoGeoms() )
    {
        bool rel = g->m_AbsRelFlag() == vsp::REL;
        m_XfGeoms.push_back( g->GetID() );
        m_XfRel.push_back( rel );
        if ( rel )
        {
            m_XfStartLoc.push_back( vec3d( g->m_XRelLoc(), g->m_YRelLoc(), g->m_ZRelLoc() ) );
            m_XfStartRot.push_back( vec3d( g->m_XRelRot(), g->m_YRelRot(), g->m_ZRelRot() ) );
        }
        else
        {
            m_XfStartLoc.push_back( vec3d( g->m_XLoc(), g->m_YLoc(), g->m_ZLoc() ) );
            m_XfStartRot.push_back( vec3d( g->m_XRot(), g->m_YRot(), g->m_ZRot() ) );
        }
        Matrix4d inv = g->getAttachMatrix();
        inv.affineInverse();
        m_XfAttachInv.push_back( inv );
    }

    // Take the keyboard, so the number goes here and not into a field elsewhere.
    Fl::focus( this );
    redraw();
}

// Follows the mouse while moving or rotating.
void VspGlWindow::XfMouse( int x, int y )
{
    glm::vec2 m( x, y );
    m_XfSnap = Fl::event_state( FL_CTRL ) != 0;
    if ( m_XfFrameOK )
    {
        // The turn round the selection, added up so it can go past half a turn.
        glm::vec2 a = m_XfMousePrev - m_XfFrame.m_Origin, b = m - m_XfFrame.m_Origin;
        float minr = 4.0f * pixels_per_unit();
        if ( glm::length( a ) > minr && glm::length( b ) > minr )
        {
            m_XfAngle += glm::degrees( atan2( a.x * b.y - a.y * b.x, glm::dot( a, b ) ) );
            m_XfMousePrev = m;
        }
        else if ( glm::length( a ) <= minr )
        {
            m_XfMousePrev = m;
        }
    }
    m_XfMouseNow = m;
    m_XfMoved = m_XfMoved || glm::length( m - m_XfMouse0 ) > 2.0f;
    if ( m_XfText.empty() )
    {
        XfApply( false );
        redraw();
    }
}

// What G or R comes to now: a typed amount, or else the mouse's travel.  A move with no axis held
// is in the view's plane: the smallest model move whose image on screen is the mouse's travel.
void VspGlWindow::XfCompute( vec3d & move, int & raxis, double & angle )
{
    move = vec3d();
    raxis = m_XfAxis >= 0 ? m_XfAxis : m_XfRotAxis;
    angle = 0.0;

    bool typed = m_XfAxis >= 0 && !m_XfText.empty();
    double val = 0.0;
    if ( typed && m_XfText != "-" && m_XfText != "." && m_XfText != "-." )
    {
        val = atof( m_XfText.c_str() );
    }

    if ( m_XfMode == XF_MOVE )
    {
        if ( typed )
        {
            move[ m_XfAxis ] = val;
            return;
        }
        if ( !m_XfFrameOK )
        {
            return;
        }
        glm::vec2 d = m_XfMouseNow - m_XfMouse0;
        if ( m_XfAxis >= 0 )
        {
            float px = m_XfFrame.m_PxPerUnit[ m_XfAxis ];
            double v = px > 1e-6f ? glm::dot( d, m_XfFrame.m_Dir[ m_XfAxis ] ) / px : 0.0;
            move[ m_XfAxis ] = m_XfSnap ? std::round( v ) : v;
            return;
        }
        // Screen travel per unit along each axis: J = [ c0 c1 c2 ].  The least-norm w with J w = d
        // is J^T ( J J^T )^-1 d.
        glm::vec2 c[3];
        for ( int k = 0; k < 3; k++ )
        {
            c[k] = m_XfFrame.m_Dir[k] * m_XfFrame.m_PxPerUnit[k];
        }
        double m00 = 0.0, m01 = 0.0, m11 = 0.0;
        for ( int k = 0; k < 3; k++ )
        {
            m00 += c[k].x * c[k].x;
            m01 += c[k].x * c[k].y;
            m11 += c[k].y * c[k].y;
        }
        double det = m00 * m11 - m01 * m01;
        if ( std::fabs( det ) < 1e-12 )
        {
            return;
        }
        double y0 = ( m11 * d.x - m01 * d.y ) / det;
        double y1 = ( m00 * d.y - m01 * d.x ) / det;
        for ( int k = 0; k < 3; k++ )
        {
            double v = c[k].x * y0 + c[k].y * y1;
            move[k] = m_XfSnap ? std::round( v ) : v;
        }
        return;
    }

    if ( typed )
    {
        angle = val;
        return;
    }
    angle = m_XfAngle * m_XfRotSign[ raxis ];
    if ( m_XfSnap )
    {
        angle = 5.0 * std::round( angle / 5.0 );
    }
}

// Sets the components to their start plus the move or rotation.  A move is along the model's
// axes: a component placed relative to its parent takes it through the parent's attachment
// rotation, as the move arrows do.  A rotation adds to that axis's rotation.  When record is set
// the change goes on the undo stack; previews don't.
void VspGlWindow::XfApply( bool record )
{
    Vehicle * veh = VehicleMgr.GetVehicle();
    if ( !veh || m_XfMode == XF_NONE )
    {
        return;
    }

    vec3d mv;
    int raxis = 2;
    double angle = 0.0;
    XfCompute( mv, raxis, angle );

    bool drag = !record;
    for ( int i = 0; i < ( int )m_XfGeoms.size(); i++ )
    {
        Geom * g = veh->FindGeom( m_XfGeoms[i] );
        if ( !g )
        {
            continue;
        }
        bool rel = m_XfRel[i];
        if ( m_XfMode == XF_MOVE )
        {
            vec3d move = mv;
            vec3d loc = m_XfStartLoc[i] + ( rel ? m_XfAttachInv[i].xformnorm( move ) : move );
            Parm * p[3] = { &g->m_XLoc, &g->m_YLoc, &g->m_ZLoc };
            if ( rel )
            {
                p[0] = &g->m_XRelLoc;
                p[1] = &g->m_YRelLoc;
                p[2] = &g->m_ZRelLoc;
            }
            for ( int k = 0; k < 3; k++ )
            {
                p[k]->SetFromDevice( loc[k], drag );
            }
        }
        else
        {
            Parm * p[3] = { &g->m_XRot, &g->m_YRot, &g->m_ZRot };
            if ( rel )
            {
                p[0] = &g->m_XRelRot;
                p[1] = &g->m_YRelRot;
                p[2] = &g->m_ZRelRot;
            }
            p[ raxis ]->SetFromDevice( m_XfStartRot[i][ raxis ] + angle, drag );
        }
    }
    m_ScreenMgr->SetUpdateFlag( true );
}

// Puts every component back where the move or rotate found it, without touching undo.
void VspGlWindow::XfRestore()
{
    Vehicle * veh = VehicleMgr.GetVehicle();
    if ( !veh )
    {
        return;
    }
    for ( int i = 0; i < ( int )m_XfGeoms.size(); i++ )
    {
        Geom * g = veh->FindGeom( m_XfGeoms[i] );
        if ( !g )
        {
            continue;
        }
        bool rel = m_XfRel[i];
        Parm * loc[3] = { &g->m_XLoc, &g->m_YLoc, &g->m_ZLoc };
        Parm * rot[3] = { &g->m_XRot, &g->m_YRot, &g->m_ZRot };
        if ( rel )
        {
            loc[0] = &g->m_XRelLoc; loc[1] = &g->m_YRelLoc; loc[2] = &g->m_ZRelLoc;
            rot[0] = &g->m_XRelRot; rot[1] = &g->m_YRelRot; rot[2] = &g->m_ZRelRot;
        }
        for ( int k = 0; k < 3; k++ )
        {
            loc[k]->SetFromDevice( m_XfStartLoc[i][k], true );
            rot[k]->SetFromDevice( m_XfStartRot[i][k], true );
        }
    }
    m_ScreenMgr->SetUpdateFlag( true );
}

// Applies (from the start, so undo returns to it) or cancels the move or rotate.
void VspGlWindow::XfEnd( bool confirm )
{
    XfRestore();
    if ( confirm && ( ( m_XfAxis >= 0 && !m_XfText.empty() ) || ( m_XfText.empty() && m_XfMoved ) ) )
    {
        XfApply( true );
    }
    m_XfMode = XF_NONE;
    m_XfAxis = -1;
    m_XfText.clear();
    m_XfGeoms.clear();
    m_XfRel.clear();
    m_XfStartLoc.clear();
    m_XfStartRot.clear();
    m_XfAttachInv.clear();
    m_ScreenMgr->SetUpdateFlag( true );
    redraw();
}

// The chosen axis as a line through the selection, in its axis colour, and what's being typed in
// the top left corner.
void VspGlWindow::XfDraw()
{
    if ( m_XfMode == XF_NONE )
    {
        return;
    }

    float ppu = pixels_per_unit();

    GLint program = 0;
    glGetIntegerv( GL_CURRENT_PROGRAM, &program );
    glUseProgram( 0 );

    glPushAttrib( GL_ALL_ATTRIB_BITS );
    glDisable( GL_SCISSOR_TEST );
    glDisable( GL_DEPTH_TEST );
    glDisable( GL_LIGHTING );
    glDisable( GL_TEXTURE_2D );
    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glEnable( GL_LINE_SMOOTH );

    glViewport( 0, 0, pixel_w(), pixel_h() );
    glMatrixMode( GL_PROJECTION );
    glPushMatrix();
    glLoadIdentity();
    glOrtho( 0, pixel_w(), 0, pixel_h(), -1, 1 );
    glMatrixMode( GL_MODELVIEW );
    glPushMatrix();
    glLoadIdentity();

    GizmoFrame f = m_XfFrame;
    if ( m_XfAxis >= 0 && m_XfFrameOK && glm::length( f.m_Dir[ m_XfAxis ] ) > 0.0f )
    {
        const float * c = GIZMO_COLOR[ m_XfAxis ];
        glColor4f( c[0], c[1], c[2], 0.85f );
        glLineWidth( 1.5f * ppu );
        float reach = 4.0f * ( pixel_w() + pixel_h() );
        glm::vec2 a = f.m_Origin - f.m_Dir[ m_XfAxis ] * reach;
        glm::vec2 b = f.m_Origin + f.m_Dir[ m_XfAxis ] * reach;
        glBegin( GL_LINES );
        glVertex2f( a.x, a.y );
        glVertex2f( b.x, b.y );
        glEnd();
    }

    vec3d mv;
    int raxis = 2;
    double angle = 0.0;
    XfCompute( mv, raxis, angle );
    char buf[160];
    string label;
    if ( m_XfMode == XF_MOVE )
    {
        if ( m_XfAxis >= 0 )
        {
            snprintf( buf, sizeof( buf ), "Move %c: %s", 'X' + m_XfAxis,
                      m_XfText.empty() ? std::to_string( mv[ m_XfAxis ] ).substr( 0, 8 ).c_str() : m_XfText.c_str() );
        }
        else
        {
            snprintf( buf, sizeof( buf ), "Move: %.3f, %.3f, %.3f", mv.x(), mv.y(), mv.z() );
        }
    }
    else
    {
        snprintf( buf, sizeof( buf ), "Rotate %c: %s deg", 'X' + raxis,
                  m_XfText.empty() ? std::to_string( angle ).substr( 0, 7 ).c_str() : m_XfText.c_str() );
    }
    label = buf;
    label += "     X/Y/Z axis, type a value, Ctrl snaps, Enter or click applies, Esc cancels";
    gl_font( FL_HELVETICA_BOLD, 16 );
    glColor4f( 1.0f, 1.0f, 1.0f, 0.95f );
    gl_draw( label.c_str(), 16.0f * ppu, pixel_h() - 28.0f * ppu );

    glMatrixMode( GL_PROJECTION );
    glPopMatrix();
    glMatrixMode( GL_MODELVIEW );
    glPopMatrix();
    glPopAttrib();

    glUseProgram( program );
}

//==== TitanVSP: ground grid ====//
// Drawn in viewport vp right after its scene, in the model's own coordinates, so parts in front of
// the grid hide it and it hides parts below it.  It lies on z = 0, centred under every component
// in the model -- hidden ones too -- and looks endless: it's solid near the model and fades out
// slowly, reaching nothing at 110% of the model's size from its middle.  Lines fall on whole
// multiples of a round spacing (1, 2 or 5 times a power of ten) that gives about twenty cells
// across the model, with every fifth line brighter and the X and Y axes in red and green.
void VspGlWindow::GridDraw( VSPGraphic::Viewport * vp )
{
    Vehicle * veh = VehicleMgr.GetVehicle();
    if ( m_LinkedScreen != DrawObj::VSP_MAIN_SCREEN || !m_GEngine || !vp || !veh || !veh->m_ViewGrid() )
    {
        return;
    }
    VSPGraphic::Camera * cam = vp->getCamera();
    if ( !cam || vp->width() <= 0 || vp->height() <= 0 )
    {
        return;
    }

    BndBox box;
    for ( Geom * g : veh->FindGeomVec( veh->GetGeomVec() ) )
    {
        if ( g )
        {
            box.Update( g->GetBndBox() );
        }
    }
    if ( box.IsEmpty() )
    {
        return;
    }

    double cx = 0.5 * ( box.GetMin( 0 ) + box.GetMax( 0 ) );
    double cy = 0.5 * ( box.GetMin( 1 ) + box.GetMax( 1 ) );
    double size = std::max( box.GetMax( 0 ) - box.GetMin( 0 ),
                  std::max( box.GetMax( 1 ) - box.GetMin( 1 ), box.GetMax( 2 ) - box.GetMin( 2 ) ) );
    if ( !( size > 0.0 ) || !std::isfinite( size ) )
    {
        return;
    }

    // Fully there within a quarter of the model's size of its middle, fading smoothly to nothing
    // at 110% of its size.
    const double rsolid = 0.25 * size;
    const double rfade = 1.1 * size;
    auto fade = [&]( double x, double y )
    {
        double r = std::sqrt( ( x - cx ) * ( x - cx ) + ( y - cy ) * ( y - cy ) );
        if ( r <= rsolid )
        {
            return 1.0;
        }
        if ( r >= rfade )
        {
            return 0.0;
        }
        double t = ( r - rsolid ) / ( rfade - rsolid );
        return 1.0 - t * t * ( 3.0 - 2.0 * t );
    };

    double raw = size / 20.0;
    double mag = std::pow( 10.0, std::floor( std::log10( raw ) ) );
    double step = mag;
    if ( raw / mag > 5.0 )
    {
        step = 10.0 * mag;
    }
    else if ( raw / mag > 2.0 )
    {
        step = 5.0 * mag;
    }
    else if ( raw / mag > 1.0 )
    {
        step = 2.0 * mag;
    }

    double x0 = cx - rfade, x1 = cx + rfade, y0 = cy - rfade, y1 = cy + rfade;

    GLint program = 0;
    glGetIntegerv( GL_CURRENT_PROGRAM, &program );
    glUseProgram( 0 );

    glPushAttrib( GL_ALL_ATTRIB_BITS );
    glDisable( GL_LIGHTING );
    glDisable( GL_TEXTURE_2D );
    glDisable( GL_ALPHA_TEST );
    glDisable( GL_CLIP_PLANE0 );
    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glShadeModel( GL_SMOOTH );
    glEnable( GL_LINE_SMOOTH );
    glEnable( GL_DEPTH_TEST );
    glDepthFunc( GL_LEQUAL );
    glDepthMask( GL_FALSE );

    glViewport( vp->x(), vp->y(), vp->width(), vp->height() );
    glMatrixMode( GL_PROJECTION );
    glPushMatrix();
    glLoadMatrixf( &cam->getProjectionMatrix()[0][0] );
    glMatrixMode( GL_MODELVIEW );
    glPushMatrix();
    glLoadMatrixf( &cam->getModelViewMatrix()[0][0] );

    float ppu = pixels_per_unit();

    // A line across the fade circle, in short pieces so its opacity follows the fade.
    const int npiece = 48;
    auto line = [&]( double ax, double ay, double bx, double by, const float * c )
    {
        glBegin( GL_LINE_STRIP );
        for ( int k = 0; k <= npiece; k++ )
        {
            double t = k / ( double )npiece;
            double x = ax + t * ( bx - ax ), y = ay + t * ( by - ay );
            glColor4f( c[0], c[1], c[2], ( float )( c[3] * fade( x, y ) ) );
            glVertex3d( x, y, 0.0 );
        }
        glEnd();
    };

    const float minor[4] = { 0.36f, 0.38f, 0.42f, 0.45f };
    const float major[4] = { 0.50f, 0.53f, 0.58f, 0.65f };

    for ( int pass = 0; pass < 2; pass++ )
    {
        const float * c = pass == 0 ? minor : major;
        glLineWidth( ( pass == 0 ? 1.0f : 1.5f ) * ppu );
        for ( long long i = ( long long )std::ceil( x0 / step ); i * step <= x1; i++ )
        {
            if ( i != 0 && ( i % 5 == 0 ) == ( pass == 1 ) )
            {
                // Only the part inside the fade circle.
                double dx = i * step - cx;
                double h = std::sqrt( std::max( rfade * rfade - dx * dx, 0.0 ) );
                line( i * step, cy - h, i * step, cy + h, c );
            }
        }
        for ( long long j = ( long long )std::ceil( y0 / step ); j * step <= y1; j++ )
        {
            if ( j != 0 && ( j % 5 == 0 ) == ( pass == 1 ) )
            {
                double dy = j * step - cy;
                double h = std::sqrt( std::max( rfade * rfade - dy * dy, 0.0 ) );
                line( cx - h, j * step, cx + h, j * step, c );
            }
        }
    }

    // The X axis (red) and Y axis (green), fading with the rest.
    glLineWidth( 2.0f * ppu );
    const float xaxis[4] = { 1.0f, 0.2f, 0.32f, 0.8f };
    const float yaxis[4] = { 0.54f, 0.86f, 0.0f, 0.8f };
    if ( std::abs( cy ) < rfade )
    {
        double h = std::sqrt( rfade * rfade - cy * cy );
        line( cx - h, 0.0, cx + h, 0.0, xaxis );
    }
    if ( std::abs( cx ) < rfade )
    {
        double h = std::sqrt( rfade * rfade - cx * cx );
        line( 0.0, cy - h, 0.0, cy + h, yaxis );
    }

    glMatrixMode( GL_PROJECTION );
    glPopMatrix();
    glMatrixMode( GL_MODELVIEW );
    glPopMatrix();
    glPopAttrib();

    glUseProgram( program );
}

//==== TitanVSP: Blanks as balls ====//

namespace
{
const float BLANK_RADIUS = 7.0f;       // Ball radius, in screen points.
const float BLANK_RING = 14.0f;        // Selection ring radius.
const float BLANK_ALPHA = 0.5f;        // The ball's opacity.

// Every shown Blank, with each symmetric copy's origin.
struct BlankOrigins
{
    Geom * m_Geom;
    vector< vec3d > m_Origins;
};

vector< BlankOrigins > ShownBlanks()
{
    vector< BlankOrigins > out;
    Vehicle * veh = VehicleMgr.GetVehicle();
    if ( !veh )
    {
        return out;
    }
    for ( Geom * g : veh->FindGeomVec( veh->GetGeomVec() ) )
    {
        if ( !g || g->GetType().m_Type != BLANK_GEOM_TYPE || !g->GetSetFlag( vsp::SET_SHOWN ) )
        {
            continue;
        }
        BlankOrigins b;
        b.m_Geom = g;
        for ( const Matrix4d & m : g->GetTransMatVec() )
        {
            b.m_Origins.push_back( m.getTranslation() );
        }
        if ( !b.m_Origins.empty() )
        {
            out.push_back( b );
        }
    }
    return out;
}
}

// Drawn in viewport vp right after its scene (LayoutMgr's scene overlay), depth-tested against
// it, so a Blank behind a part is hidden by it as the part's own surfaces would be.
void VspGlWindow::BlankDraw( VSPGraphic::Viewport * vp )
{
    if ( m_LinkedScreen != DrawObj::VSP_MAIN_SCREEN || !m_GEngine || !vp )
    {
        return;
    }
    vector< BlankOrigins > blanks = ShownBlanks();
    if ( blanks.empty() )
    {
        return;
    }
    Vehicle * veh = VehicleMgr.GetVehicle();

    float ppu = pixels_per_unit();
    float r = BLANK_RADIUS * ppu;
    float rr = BLANK_RING * ppu;

    GLint program = 0;
    glGetIntegerv( GL_CURRENT_PROGRAM, &program );
    glUseProgram( 0 );

    glPushAttrib( GL_ALL_ATTRIB_BITS );
    glDisable( GL_LIGHTING );
    glDisable( GL_TEXTURE_2D );
    glDisable( GL_CULL_FACE );
    glDisable( GL_ALPHA_TEST );
    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glShadeModel( GL_SMOOTH );
    glEnable( GL_LINE_SMOOTH );
    glDepthMask( GL_FALSE );
    glEnable( GL_DEPTH_TEST );
    glDepthFunc( GL_LEQUAL );

    glViewport( 0, 0, pixel_w(), pixel_h() );
    glMatrixMode( GL_PROJECTION );
    glPushMatrix();
    glLoadIdentity();
    // Window x and y in pixels, and z = -depth, so a vertex at a projected point's window depth
    // lands on that depth in the depth buffer.
    glOrtho( 0, pixel_w(), 0, pixel_h(), 0, 1 );
    glMatrixMode( GL_MODELVIEW );
    glPushMatrix();
    glLoadIdentity();

    glEnable( GL_SCISSOR_TEST );

    const int nseg = 32;
    {
        VSPGraphic::Camera * cam = vp->getCamera();
        if ( !cam || vp->width() <= 0 || vp->height() <= 0 )
        {
            glMatrixMode( GL_PROJECTION );
            glPopMatrix();
            glMatrixMode( GL_MODELVIEW );
            glPopMatrix();
            glPopAttrib();
            glUseProgram( program );
            return;
        }
        glScissor( vp->x(), vp->y(), vp->width(), vp->height() );

        glm::mat4 mv = cam->getModelViewMatrix();
        glm::mat4 pr = cam->getProjectionMatrix();
        glm::vec4 view( vp->x(), vp->y(), vp->width(), vp->height() );

        for ( const BlankOrigins & b : blanks )
        {
            bool selected = veh && veh->IsGeomActive( b.m_Geom->GetID() );

            vec3d wc = b.m_Geom->m_GuiDraw.GetWireColor() / 255.0;
            float c[3] = { ( float )wc.x(), ( float )wc.y(), ( float )wc.z() };
            if ( selected )
            {
                for ( float & ch : c )
                {
                    ch = ch + ( 1.0f - ch ) / 3.0f;
                }
            }

            for ( const vec3d & o : b.m_Origins )
            {
                glm::vec3 w = glm::project( glm::vec3( o.x(), o.y(), o.z() ), mv, pr, view );
                if ( w.z < 0.0f || w.z > 1.0f )
                {
                    continue;
                }
                float z = -( w.z - 1.0e-4f );

                // The ball: lit from the upper left, fading to a darker rim.
                glBegin( GL_TRIANGLE_FAN );
                glColor4f( std::min( c[0] * 1.3f + 0.15f, 1.0f ), std::min( c[1] * 1.3f + 0.15f, 1.0f ),
                           std::min( c[2] * 1.3f + 0.15f, 1.0f ), BLANK_ALPHA );
                glVertex3f( w.x - 0.3f * r, w.y + 0.3f * r, z );
                glColor4f( c[0] * 0.5f, c[1] * 0.5f, c[2] * 0.5f, BLANK_ALPHA );
                for ( int k = 0; k <= nseg; k++ )
                {
                    float t = 2.0f * 3.14159265f * k / nseg;
                    glVertex3f( w.x + r * std::cos( t ), w.y + r * std::sin( t ), z );
                }
                glEnd();

                // A fine dark edge, so the ball reads on a light background too.
                glColor4f( 0.0f, 0.0f, 0.0f, 0.5f * BLANK_ALPHA );
                glLineWidth( 1.0f * ppu );
                glBegin( GL_LINE_LOOP );
                for ( int k = 0; k < nseg; k++ )
                {
                    float t = 2.0f * 3.14159265f * k / nseg;
                    glVertex3f( w.x + r * std::cos( t ), w.y + r * std::sin( t ), z );
                }
                glEnd();

                if ( selected )
                {
                    glColor4f( 1.0f, 1.0f, 1.0f, 0.75f );
                    glLineWidth( 2.0f * ppu );
                    glBegin( GL_LINE_LOOP );
                    for ( int k = 0; k < nseg; k++ )
                    {
                        float t = 2.0f * 3.14159265f * k / nseg;
                        glVertex3f( w.x + rr * std::cos( t ), w.y + rr * std::sin( t ), z );
                    }
                    glEnd();
                }
            }
        }
    }

    glMatrixMode( GL_PROJECTION );
    glPopMatrix();
    glMatrixMode( GL_MODELVIEW );
    glPopMatrix();
    glPopAttrib();

    glUseProgram( program );
}

// The Blank whose ball is nearest ( x, y ) in the selected viewport, within a few points of it.
string VspGlWindow::BlankHit( int x, int y )
{
    if ( m_LinkedScreen != DrawObj::VSP_MAIN_SCREEN || !m_GEngine )
    {
        return string();
    }
    VSPGraphic::Viewport * vp = m_GEngine->getDisplay()->getViewport();
    VSPGraphic::Camera * cam = vp ? vp->getCamera() : nullptr;
    if ( !cam )
    {
        return string();
    }
    glm::mat4 mv = cam->getModelViewMatrix();
    glm::mat4 pr = cam->getProjectionMatrix();
    glm::vec4 view( vp->x(), vp->y(), vp->width(), vp->height() );

    string best;
    float bestd = ( BLANK_RADIUS + 4.0f ) * pixels_per_unit();
    for ( const BlankOrigins & b : ShownBlanks() )
    {
        for ( const vec3d & o : b.m_Origins )
        {
            glm::vec3 w = glm::project( glm::vec3( o.x(), o.y(), o.z() ), mv, pr, view );
            if ( w.z < 0.0f || w.z > 1.0f )
            {
                continue;
            }
            float d = glm::length( glm::vec2( w.x - x, w.y - y ) );
            if ( d < bestd )
            {
                bestd = d;
                best = b.m_Geom->GetID();
            }
        }
    }
    return best;
}

//==== TitanVSP: click to select ====//
// Picks what's under ( x, y ) the way the Geom Browser's Pick mode does, but only for this click:
// the browser offers every visible component for picking, the scene is rebuilt with them and
// drawn once in pick colours, and the colour under the click names the component.  Then picking
// is switched off again, so dragging still turns the view and nothing highlights on hover.
void VspGlWindow::ClickSelect( int x, int y, bool toggle )
{
    ManageGeomScreen * geomScreen = dynamic_cast< ManageGeomScreen * >( m_ScreenMgr->GetScreen( vsp::VSP_MANAGE_GEOM_SCREEN ) );
    Vehicle * veh = VehicleMgr.GetVehicle();
    if ( !geomScreen || !veh || !m_GEngine )
    {
        return;
    }

    make_current();

    // A Blank's ball is small and drawn over its surroundings; it wins if it's under the click.
    string blank = BlankHit( x, y );

    geomScreen->SetClickPick( true );
    update();

    VSPGraphic::Scene * scene = m_GEngine->getScene();
    m_GEngine->getDisplay()->predraw( scene, x, y );

    string picked;
    if ( scene->selectHighlight() )
    {
        Selectable * selected = scene->getLastSelected();
        SelectedGeom * geom = dynamic_cast< SelectedGeom * >( selected );
        if ( geom )
        {
            ID * id = _findID( geom->getSource()->getID() );
            if ( id )
            {
                picked = id->geomID.substr( 0, id->geomID.find_last_of( '_' ) );
            }
        }
        if ( selected )
        {
            scene->removeSelected( selected );
        }
    }

    geomScreen->SetClickPick( false );
    update();

    if ( !blank.empty() )
    {
        picked = blank;
    }

    vector< string > sel = veh->GetActiveGeomVec();
    if ( !picked.empty() && !veh->FindGeom( picked ) )
    {
        picked.clear();
    }

    if ( toggle )
    {
        if ( picked.empty() )
        {
            return;
        }
        auto it = std::find( sel.begin(), sel.end(), picked );
        if ( it != sel.end() )
        {
            sel.erase( it );
        }
        else
        {
            sel.push_back( picked );
        }
    }
    else
    {
        sel.clear();
        if ( !picked.empty() )
        {
            sel.push_back( picked );
        }
    }

    geomScreen->SetSelection( sel );
    redraw();

    // Selecting opens the component's window on the next update, and Windows hands it the
    // keyboard.  Take it back for the view, so G and R work straight away.  Geom windows are owned
    // by the main window, so they stay in front of it.  Twice, in case the window is slow to
    // appear the first time.
    Fl::add_timeout( 0.15, RefocusCB, this );
    Fl::add_timeout( 0.5, RefocusCB, this );
}

void VspGlWindow::RefocusCB( void * data )
{
    VspGlWindow * win = ( VspGlWindow * )data;
    Fl_Window * top = win ? win->top_window() : nullptr;
    if ( !top || !top->shown() )
    {
        return;
    }
#ifdef _WIN32
    HWND hwnd = fl_win32_xid( top );
    if ( hwnd && GetForegroundWindow() != hwnd )
    {
        SetForegroundWindow( hwnd );
    }
#endif
    Fl::focus( win );
}

int VspGlWindow::OnWheelScroll( int dx, int dy, int x, int y )
{
    int handled = 1;

    float zoomChange = 1;
    float zoomSpeed = 1.15; // size increase when zooming in

    if ( dy > 0 ) // wheel pulled toward user
    {
        zoomChange = zoomSpeed;
    }
    else if ( dy < 0 ) // wheel pushed away from user
    {
        zoomChange = 1 / zoomSpeed;
    }

    if ( zoomChange != 1 )
    {
        Viewport* viewport = m_GEngine->getDisplay()->getViewport();
        float width = viewport->width();
        float height = viewport->height();
        int mx = x - viewport->x();
        int my = y - viewport->y();

        float zoom = getRelativeZoomValue();
        glm::vec2 pan = getPanValues();

        float mousePosBeforeX, mousePosBeforeY;
        mousePosBeforeX = ( ( ( zoom * width ) * ( ( mx / width ) - 0.5 ) - pan.x ) );
        mousePosBeforeY = ( ( ( zoom * height ) * ( ( my / height ) - 0.5 ) - pan.y ) );

        relativeZoom( getRelativeZoomValue() * zoomChange );

        zoom *= zoomChange;
        pan *= zoomChange;

        float mousePosAfterX, mousePosAfterY;
        mousePosAfterX = ( ( ( zoom * width ) * ( ( mx / width ) - 0.5 ) - pan.x ) );
        mousePosAfterY = ( ( ( zoom * height ) * ( ( my / height ) - 0.5 ) - pan.y )  );

        // cancel out any effect the zoom had on pan position, so that the same location remains under the mouse
        relativePan( pan.x + mousePosAfterX - mousePosBeforeX , pan.y + mousePosAfterY - mousePosBeforeY );

        UpdateZoomParms();
        UpdatePanParms();

        redraw();
    }

    return handled;
}

void VspGlWindow::_sendFeedback( Selectable * selected )
{
    // Find out where feedback is heading...
    std::string selectedFeedbackName = selected->getGroup();

    // Probe Screen Feedback
    ManageMeasureScreen * measureScreen = dynamic_cast<ManageMeasureScreen*>
    ( m_ScreenMgr->GetScreen( vsp::VSP_MEASURE_SCREEN ) );

    if( measureScreen && measureScreen->getFeedbackGroupName() == selectedFeedbackName )
    {
        // Location feedback
        SelectedLoc * loc = dynamic_cast<SelectedLoc*> ( selected );
        if( loc )
        {
            glm::vec3 placement = loc->getLoc();
            measureScreen->Set( vec3d( placement.x, placement.y, placement.z ) );

            // Only one selection is needed for label, remove this 'selected' from selection list.
            m_GEngine->getScene()->removeSelected( selected );
            selected = nullptr;
        }

        // Vertex feedback
        // Cast selectable to SelectedPnt object, so that we can get Render Source Ptr.
        SelectedPnt * pnt = dynamic_cast<SelectedPnt*>( selected );
        if( pnt )
        {
            VSPGraphic::Renderable * e = dynamic_cast<VSPGraphic::Renderable*>(pnt->getSource());
            if(e)
            {
                ID * id = _findID( e->getID() );
                if( id )
                {
                    unsigned int index = id->geomID.find_first_of( '_' );
                    std::string baseId = id->geomID.substr( 0, index );
                    glm::vec3 placement = e->getVertexVec(pnt->getIndex());
                    measureScreen->Set( vec3d( placement.x, placement.y, placement.z ), baseId );

                    // Only one selection is needed for label, remove this 'selected' from selection list.
                    m_GEngine->getScene()->removeSelected( selected );
                    selected = nullptr;
                }
            }
        }
    }

    // Geom Screen Feedback
    ManageGeomScreen * geomScreen = dynamic_cast<ManageGeomScreen *>
        ( m_ScreenMgr->GetScreen( vsp::VSP_MANAGE_GEOM_SCREEN ) );

    if( geomScreen && geomScreen->getFeedbackGroupName() == selectedFeedbackName )
    {
        SelectedGeom * geom = dynamic_cast<SelectedGeom*>( selected );
        if( geom )
        {
            ID * id = _findID( geom->getSource()->getID() );
            if( id )
            {
                unsigned int index = id->geomID.find_last_of( '_' );
                std::string baseId = id->geomID.substr( 0, index );
                geomScreen->Set( baseId );

                // Only one selection is needed for Geom, remove this 'selected' from selection list.
                m_GEngine->getScene()->removeSelected( selected );
                selected = nullptr;
            }
        }
        // Mac Fix:  On Mac, FLTK window is always focus on the last shown GUI.  In
        // this case where a geometry is selected, geometry's window becomes the new
        // focus.  The behavior locks up geometry selection process.  Set OpenGL
        // window back on focus so user can proceed without interruption.
        focus(this);
    }

    // Routing Screen Feedback
    if ( geomScreen )
    {
        RoutingScreen * routingScreen = dynamic_cast<RoutingScreen*>
            ( geomScreen->GetGeomScreen( vsp::ROUTING_GEOM_SCREEN ) );

        if( routingScreen && routingScreen->getFeedbackGroupName() == selectedFeedbackName )
        {
            // Vertex feedback
            // Cast selectable to SelectedPnt object, so that we can get Render Source Ptr.
            SelectedPnt * pnt = dynamic_cast<SelectedPnt*>( selected );
            if( pnt )
            {
                VSPGraphic::Renderable * e = dynamic_cast<VSPGraphic::Renderable*>(pnt->getSource());
                if(e)
                {
                    ID * id = _findID( e->getID() );
                    if( id )
                    {
                        unsigned int index = id->geomID.find_first_of( '_' );
                        std::string baseId = id->geomID.substr( 0, index );
                        glm::vec3 placement = e->getVertexVec(pnt->getIndex());
                        routingScreen->Set( vec3d( placement.x, placement.y, placement.z ), baseId );

                        // Only one selection is needed for label, remove this 'selected' from selection list.
                        m_GEngine->getScene()->removeSelected( selected );
                        selected = nullptr;
                    }
                }
            }
        }
    }

    // Center of rotation Screen Feedback
    ManageCORScreen * corScreen = dynamic_cast<ManageCORScreen*>
        ( m_ScreenMgr->GetScreen( vsp::VSP_COR_SCREEN ) );

    if( corScreen && corScreen->getFeedbackGroupName() == selectedFeedbackName )
    {
        SelectedPnt * pnt = dynamic_cast<SelectedPnt*>( selected );

        if( pnt )
        {
            VSPGraphic::Renderable * entity = dynamic_cast<VSPGraphic::Renderable*>( pnt->getSource() );
            if( entity )
            {
                glm::vec3 placement = entity->getVertexVec( pnt->getIndex() );

                // Set COR for both call types
                if ( corScreen->getFeedbackGroupName() == "CORGUIGroup" ||
                     corScreen->getFeedbackGroupName() == "VNGUIGroup" )
                {
                    m_GEngine->getDisplay()->setCOR( -placement.x, -placement.y, -placement.z );
                    m_GEngine->getDisplay()->center();

                    UpdateCORParms();
                    UpdatePanParms();
                }

                // Set View Normal
                if ( corScreen->getFeedbackGroupName() == "VNGUIGroup" )
                {

                    ID * id = _findID( entity->getID() );
                    if( id )
                    {
                        unsigned int index = id->geomID.find_first_of( '_' );
                        std::string baseId = id->geomID.substr( 0, index );
                        Geom* geom_ptr = VehicleMgr.GetVehicle()->FindGeom( baseId );

                        if ( geom_ptr )
                        {
                            glm::vec3 placement = entity->getVertexVec( pnt->getIndex() );

                            Matrix4d norm_basis;
                            vec3d norm_angles;

                            if ( geom_ptr->isNonSurfaceType() )
                            {
                                // defer to model matrix if no surface
                                norm_basis = geom_ptr->getModelMatrix();

                                // rotate to face front of object
                                norm_basis.rotateY( -90. );
                                norm_basis.rotateZ( -90. );
                            }
                            else
                            {
                                // extract normal vector from surface
                                vec3d camDir, camRight, camUp, worldUp;
                                double u, w;

                                int surf_index = stoi( id->geomID.substr( index + 1, id->geomID.size() ) );
                                geom_ptr->ProjPnt01I( vec3d( placement.x, placement.y, placement.z ), surf_index, u, w );
                                const VspSurf* surf = geom_ptr->GetSurfPtr( surf_index );

                                worldUp = vec3d( 0., 0., 1. );
                                camDir = surf->CompNorm01( u, w );
                                camRight = cross( worldUp, camDir );
                                camUp = cross( camDir, camRight );

                                camRight.normalize();
                                camUp.normalize();

                                norm_basis.setBasis( camRight, camUp, camDir );
                            }

                            norm_angles = norm_basis.getArcballAngles();

                            getGraphicEngine()->getDisplay()->getCamera()->rotateSphere(
                                norm_angles.x(),
                                norm_angles.y(),
                                norm_angles.z()
                            );

                            UpdateAllViewParms();
                        }
                    }
                }
                // This is a dummy call to let corScreen know the job is done.
                corScreen->Set();

                // Only one selection is needed for Center of Rotation, remove this 'selected' from selection list.
                m_GEngine->getScene()->removeSelected( selected );
                selected = nullptr;
            }
        }
    }

    // FitModel Feedback
    if ( FitModelMgr.getFeedbackGroupName() == selectedFeedbackName )
    {
        SelectedPnt * pnt = dynamic_cast<SelectedPnt*>( selected );
        if( pnt )
        {
            VSPGraphic::Marker * marker = dynamic_cast<VSPGraphic::Marker*>(pnt->getSource());
            if(marker)
            {
                ID *mid = _findID( marker->getID() );
                FitModelMgr.SelectPoint( mid->geomID, pnt->getIndex() );

                m_GEngine->getScene()->removeSelected( selected );
                selected = nullptr;
            }
        }
    }
}

void VspGlWindow::_sendFeedback( const std::vector<Selectable *> &listOfSelected )
{
    for ( int i = 0; i < (int) listOfSelected.size(); i++ )
    {
        _sendFeedback( listOfSelected[i] );
    }
}

//**********************************************************************//
//*************************** EditXSecWindow ***************************//
//**********************************************************************//

EditXSecWindow::EditXSecWindow( int x, int y, int w, int h, ScreenMgr* mgr )
    : VspGlWindow( x, y, w, h, mgr, DrawObj::VSP_EDIT_CURVE_SCREEN )
{
    m_LastHit = -1;
    m_noRotate = true;
}

void EditXSecWindow::update()
{
    Vehicle* vPtr = VehicleMgr.GetVehicle();

    if ( vPtr )
    {
        make_current();

        // Get Render Objects from Vehicle.
        vector<DrawObj*> drawObjs;
        vPtr->LoadDrawObjs( drawObjs );

        // Load Render Objects for Curve Edit Screen
        CurveEditScreen* curve_editor = dynamic_cast <CurveEditScreen*>
            ( m_ScreenMgr->GetScreen( vsp::VSP_CURVE_EDIT_SCREEN ) );
        if ( curve_editor )
        {
            curve_editor->LoadDrawObjs( drawObjs );
        }

        // Load Objects to Renderer.
        _update( drawObjs ); // VspGlWindow
    }
}

int EditXSecWindow::handle( int fl_event )
{
    int ret_val = VspGlWindow::handle( fl_event );

    CurveEditScreen* curve_editor = dynamic_cast <CurveEditScreen*>
        ( m_ScreenMgr->GetScreen( vsp::VSP_CURVE_EDIT_SCREEN ) );
    if ( !curve_editor )
    {
        return ret_val;
    }

    XSecCurve* xsc = curve_editor->GetXSecCurve();

    if ( !xsc || xsc->GetType() != vsp::XS_EDIT_CURVE )
    {
        return ret_val;
    }

    EditCurveXSec* edit_curve_xs = dynamic_cast<EditCurveXSec*>( xsc );
    assert( edit_curve_xs );


    int izero = -1;
    if ( edit_curve_xs->m_View() == vsp::VIEW_FRONT ) // X,Y
    {
        izero = 2;
    }
    else if ( edit_curve_xs->m_View() == vsp::VIEW_TOP ) // X,Z
    {
        izero = 1;
    }
    else if ( edit_curve_xs->m_View() == vsp::VIEW_LEFT ) // Z,Y
    {
        izero = 0;
    }


    if ( fl_event == FL_PUSH && !Fl::event_button2() )
    {
        vec3d coord = PixelToCoord( m_mouse_x, m_mouse_y );

        vec3d scalefac( 1, 1, 1 );
        if ( !edit_curve_xs->m_AbsoluteFlag() )
        {

            scalefac.scale_x( edit_curve_xs->GetWidth() );
            scalefac.scale_y( edit_curve_xs->GetHeight() );
            scalefac.scale_z( edit_curve_xs->m_Depth() );
            scalefac[izero] = 0;
        }

        // Diameter of point + 10% considered "hit"
        double hit_r = pixels_per_unit() * 1.2 * edit_curve_xs->m_XSecPointSize.Get() / 2;

        float zoom = getRelativeZoomValue();
        m_LastHit = ihit( coord, hit_r * zoom, scalefac );

        if ( m_LastHit != -1 )
        {
            edit_curve_xs->SetSelectPntID( m_LastHit );
            curve_editor->UpdateIndexSelector( m_LastHit, false );
        }

        if ( curve_editor->GetDeleteActive() )
        {
            curve_editor->SetDeleteActive( false );

            if ( m_LastHit >= 0 )
            {
                edit_curve_xs->DeletePt( m_LastHit );
            }
        }

        if ( curve_editor->GetSplitActive() )
        {
            curve_editor->SetSplitActive( false );

            // Scale by width and height
            vec3d split_pnt = coord;
            if ( !edit_curve_xs->m_AbsoluteFlag() )
            {
                split_pnt.scale_x( edit_curve_xs->GetWidth() );
                split_pnt.scale_y( edit_curve_xs->GetHeight() );
                split_pnt.scale_z( edit_curve_xs->m_Depth() );
            }

            double u_curve;
            VspCurve base_curve = edit_curve_xs->GetBaseEditCurve();

            base_curve.OffsetX( -0.5 * edit_curve_xs->GetWidth() );
            base_curve.ZeroI( izero );
            base_curve.FindNearest01( u_curve, split_pnt );

            edit_curve_xs->m_SplitU = u_curve;
            int new_pnt = edit_curve_xs->Split01();
            curve_editor->UpdateIndexSelector( new_pnt );
        }
    }
    else if ( fl_event == FL_DRAG && m_LastHit != -1 && !Fl::event_button2() )
    {
        vec3d coord = PixelToCoord( m_mouse_x, m_mouse_y );

        // Scale by width and height
        vec3d move_pnt = coord;
        if ( edit_curve_xs->m_AbsoluteFlag() )
        {
            move_pnt.scale_x( 1.0 / edit_curve_xs->GetWidth() );
            move_pnt.scale_y( 1.0 / edit_curve_xs->GetHeight() );
            move_pnt.scale_z( 1.0 / edit_curve_xs->m_Depth() );
        }

        edit_curve_xs->MovePnt( move_pnt, izero );

        curve_editor->SetFreezeAxis( true );
    }
    else if ( fl_event == FL_RELEASE )
    {
        curve_editor->SetFreezeAxis( false );
        // Force update the parent Geom once the point is released
        edit_curve_xs->ParmChanged( nullptr, Parm::SET_FROM_DEVICE );
    }

    if ( fl_event == FL_RELEASE || fl_event == FL_DRAG || fl_event == FL_PUSH )
    {
        curve_editor->Update();
    }

    return ret_val;
}


vec3d EditXSecWindow::PixelToCoord( int x_pix, int y_pix )
{
    vec3d coord;

    //==== Find EditCurveXSec Ptr ====//
    CurveEditScreen* curve_editor = dynamic_cast <CurveEditScreen*>
        ( m_ScreenMgr->GetScreen( vsp::VSP_CURVE_EDIT_SCREEN ) );
    if ( !curve_editor )
    {
        return coord;
    }

    XSecCurve* xsc = curve_editor->GetXSecCurve();

    if ( !xsc || xsc->GetType() != vsp::XS_EDIT_CURVE )
    {
        return coord;
    }

    EditCurveXSec* edit_curve_xs = dynamic_cast<EditCurveXSec*>( xsc );
    assert( edit_curve_xs );

    float zoom = getRelativeZoomValue();
    glm::vec2 pan = getPanValues();
    double w = pixel_w();
    double h = pixel_h();

    // Convert pixels to normalized coordinates
    double data1 = ( zoom * w ) * ( ( x_pix / w ) - 0.5 ) - pan.x;
    double data2 = ( zoom * h ) * ( ( y_pix / h ) - 0.5 ) - pan.y;

    // Modulate 2D projection into 3D vector
    if ( edit_curve_xs->m_View() == vsp::VIEW_FRONT ) // X,Y
    {
        coord.set_xyz( data1, data2, 0.0 );
    }
    else if ( edit_curve_xs->m_View() == vsp::VIEW_TOP ) // X,Z
    {
        coord.set_xyz( data1, 0.0, data2 );
    }
    else if ( edit_curve_xs->m_View() == vsp::VIEW_LEFT ) // Z,Y
    {
        coord.set_xyz( 0.0, data2, data1 );
    }

    // Non-dimensionalize coordinates if needed.
    if ( !edit_curve_xs->m_AbsoluteFlag() )
    {
        coord.scale_x( 1.0 / edit_curve_xs->GetWidth() );
        coord.scale_y( 1.0 / edit_curve_xs->GetHeight() );
        coord.scale_z( 1.0 / edit_curve_xs->m_Depth() );
    }

    return coord;
}

int EditXSecWindow::ihit( const vec3d & mpt, double r_test, const vec3d &scale )
{
    //==== Find EditCurveXSec Ptr ====//
    CurveEditScreen* curve_editor = dynamic_cast <CurveEditScreen*>
        ( m_ScreenMgr->GetScreen( vsp::VSP_CURVE_EDIT_SCREEN ) );
    if ( !curve_editor )
    {
        return -1;
    }

    XSecCurve* xsc = curve_editor->GetXSecCurve();

    if ( !xsc || xsc->GetType() != vsp::XS_EDIT_CURVE )
    {
        return -1;
    }

    EditCurveXSec* edit_curve_xs = dynamic_cast<EditCurveXSec*>( xsc );
    assert( edit_curve_xs );

    bool symflag = ( edit_curve_xs->m_SymType() == vsp::SYM_RL );

    vector < vec3d > control_pts = edit_curve_xs->GetCtrlPntVec( false );
    int ndata = (int)control_pts.size();

    vector < vec3d > cpproj = proj_pt_vec( control_pts );

    double min_dist = 1e9;
    int i_hit = -1;

    for ( int i = 0; i < ndata; i++ )
    {
        if ( !symflag || control_pts[i].x() >= 0 )
        {
            vec3d delta = cpproj[i] - mpt;
            delta.scale_x( scale.x() );
            delta.scale_y( scale.y() );
            delta.scale_z( scale.z() );

            double dist_out = delta.mag();
            if ( dist_out < min_dist )
            {
                min_dist = dist_out;

                if ( dist_out < r_test )
                {
                    i_hit = i;
                }
            }
        }
    }

    return i_hit;
}

vector < vec3d > EditXSecWindow::proj_pt_vec( const vector < vec3d > & pt_in )
{
    //==== Find EditCurveXSec Ptr ====//
    CurveEditScreen* curve_editor = dynamic_cast <CurveEditScreen*>
            ( m_ScreenMgr->GetScreen( vsp::VSP_CURVE_EDIT_SCREEN ) );
    if ( !curve_editor )
    {
        return pt_in;
    }

    XSecCurve* xsc = curve_editor->GetXSecCurve();

    if ( !xsc || xsc->GetType() != vsp::XS_EDIT_CURVE )
    {
        return pt_in;
    }

    EditCurveXSec* edit_curve_xs = dynamic_cast<EditCurveXSec*>( xsc );

    vector < vec3d > pt_out = pt_in;

    int izero = -1;
    if ( edit_curve_xs->m_View() == vsp::VIEW_FRONT ) // X,Y
    {
        izero = 2;
    }
    else if ( edit_curve_xs->m_View() == vsp::VIEW_TOP ) // X,Z
    {
        izero = 1;
    }
    else if ( edit_curve_xs->m_View() == vsp::VIEW_LEFT ) // Z,Y
    {
        izero = 0;
    }

    int npt = pt_in.size();
    for ( int i = 0; i < npt; i++ )
    {
        // Non-dimensionalize control point if needed.
        if ( !edit_curve_xs->m_AbsoluteFlag() )
        {
            pt_out[i].set_x( pt_out[i].x() / edit_curve_xs->GetWidth() );
            pt_out[i].set_y( pt_out[i].y() / edit_curve_xs->GetHeight() );
            pt_out[i].set_z( pt_out[i].z() / edit_curve_xs->m_Depth() );
        }

        // Project into 2D view
        pt_out[i].v[izero] = 0.0;
    }

    return pt_out;
}

void EditXSecWindow::InitZoom()
{
    assert( m_ScreenMgr );

    VSPGraphic::Viewport* viewport = getGraphicEngine()->getDisplay()->getViewport();
    assert( viewport );

    //==== Find EditCurveXSec Ptr ====//
    CurveEditScreen* curve_editor = dynamic_cast <CurveEditScreen*>
        ( m_ScreenMgr->GetScreen( vsp::VSP_CURVE_EDIT_SCREEN ) );
    if ( !curve_editor )
    {
        return;
    }

    XSecCurve* xsc = curve_editor->GetXSecCurve();

    if ( !xsc || xsc->GetType() != vsp::XS_EDIT_CURVE )
    {
        return;
    }

    EditCurveXSec* edit_curve_xs = dynamic_cast<EditCurveXSec*>( xsc );
    assert( edit_curve_xs );

    double gl_w = pixel_w();
    double gl_h = pixel_h();
    float oz;

    double wh = max( edit_curve_xs->GetWidth(), edit_curve_xs->GetHeight() );
    oz = ( 1.5 * wh ) * ( gl_w < gl_h ? 1.f / gl_w : 1.f / gl_h );

    getGraphicEngine()->getDisplay()->getCamera()->relativeZoom( oz );
}

} // Close out namespace VSPGUI

