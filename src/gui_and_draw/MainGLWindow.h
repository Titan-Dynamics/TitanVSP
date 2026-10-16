#ifndef VSP_GUI_GL_WINDOW_H
#define VSP_GUI_GL_WINDOW_H

#include <FL/Fl.H>
#include <FL/Fl_Gl_Window.H>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>

#include <glm/glm.hpp>

#include "Vec3d.h"
#include "Common.h"
#include "Camera.h"

#include <chrono>
#include <functional>

#include "DrawObj.h"

namespace VSPGraphic
{
class GraphicEngine;
class Renderable;
class Selectable;
class Viewport;
}

class ScreenMgr;

namespace VSPGUI
{
class VspGlWindow : public Fl_Gl_Window
{
public:
    /*
    * Constructor.
    * drawObjScreen - specific drawObj screen to link.
    */
    VspGlWindow( int x, int y, int w, int h, ScreenMgr * mgr, DrawObj::ScreenEnum drawObjScreen );
    /*
    * Destructor.
    */
    virtual ~VspGlWindow();

// Compatibility for FLTK before 1.3.4
#if FL_API_VERSION < 10304
    int pixel_w() { return w(); }
    int pixel_h() { return h(); }
#endif

public:
    /*!
    * Set Window Layout.
    * Row - Number of Rows.
    * Column - Number of Columns.
    */
    virtual void setWindowLayout( int row, int column );
    /*!
    * Set view for current selected viewport.
    * Acceptable type are VSP_CAM_TOP, VSP_CAM_FRONT, VSP_CAM_LEFT, VSP_CAM_LEFT_ISO,
    * VSP_CAM_BOTTOM, VSP_CAM_REAR, VSP_CAM_RIGHT, VSP_CAM_RIGHT_ISO, VSP_CAM_CENTER.
    */
    virtual void setView( VSPGraphic::Common::VSPenum type );

public:
    /*!
    * Get graphic engine of this window.
    */
    VSPGraphic::GraphicEngine * getGraphicEngine()
    {
        return m_GEngine;
    }

public:

    /*!
     * Sets the Center of Rotation for the Camera.
     *
     * @param center center of rotation.
     */
    virtual void setCOR( glm::vec3 center );

    /*!
      * Retrieves the center of rotation values.
      * @return point of rotation.
      */
    virtual glm::vec3 getCOR();

    /*!
     * Pan current selected camera.
     *
     * x - Direction on x-axis. Positive moves right, negative moves left.
     * y - Direction on y-axis. Positive moves up, negative moves left.
     */
    virtual void relativePan( float x, float y );

    /*!
     * Retrieves the pan values as a glm vec2
     * x - Distance in x direction.
     * y - Distance in y direction.
     */
    virtual glm::vec2 getPanValues();

    /*!
    * Zoom current selected camera relative to a point in world space.
    *
    * delta - Zoom in or out.  Positive zooms out, negative zooms in.
    */
    virtual void relativeZoom( float zoomValue );

    /*!
     * Get Relative Zoom Value
     */
    virtual float getRelativeZoomValue();

    /*!
    * Rotate about an axis
    *
    * angle - Angle about rotation.
    * rotAxis - Axis of rotation.
    */
    virtual void rotateSphere( float angleX, float angleY, float angleZ );

    /*!
     * Retrieves euler angles, yitch as x, yaw as y, roll as z.
     */
    virtual glm::vec3 getRotationEulerAngles();

// Override Fl_Gl_Window Functions.
public:
    virtual void draw();
    virtual int  handle( int fl_event );

// Util Functions.
public:
    virtual void update();

    virtual void LoadAllDrawObjs( vector< DrawObj* > & draw_obj_vec );

    virtual void ResetDrawObjsGeomChangedFlags( vector< DrawObj* > & drawObjs, bool flag = false );

    virtual void ResetDrawObjsGeomChangedFlags( bool flag = false );

    virtual void clearScene();

    void UpdateViewportParms();
    void UpdateCORParms();
    void UpdatePanParms();
    void UpdateZoomParms();
    void UpdateRotationParms();
    void UpdateAllViewParms();

    // TitanVSP: turns the selected viewport's camera to a view over a fraction of a second.  With
    // fit, it also centres the visible model and zooms to frame it in that view.  done runs once
    // the camera arrives, unless another view change or the mouse interrupts it.
    void AnimateView( VSPGraphic::Common::VSPenum type, bool fit, std::function< void() > done );
    void StopViewAnim();

// Private helper functions.
private:
    void _initGLEW();

    void _updateTextures( DrawObj * drawObj );

    static void _loadTrisData( VSPGraphic::Renderable * destObj, DrawObj * drawObj );
    static void _loadQuadsData( VSPGraphic::Renderable * destObj, DrawObj * drawObj );
    static void _loadXSecData( VSPGraphic::Renderable * destObj, DrawObj * drawObj );
    static void _loadMarkData( VSPGraphic::Renderable * destObj, DrawObj * drawObj );

    void _setLighting( DrawObj * drawObj );

    void _setClipping( DrawObj * drawObj );

    struct ID;

    ID * _findID( const std::string &geomID );
    ID * _findID( unsigned int bufferID );

    void _updateBuffer( const std::vector<DrawObj *> &objects );

    void _sendFeedback( VSPGraphic::Selectable * selected );
    void _sendFeedback( const std::vector<VSPGraphic::Selectable *> &listOfSelected );

    void OnPush( int x, int y );
    void OnDrag( int x, int y );
    void OnRelease( int x, int y );
    int OnKeyup( int x, int y );
    int OnKeydown();
    int OnWheelScroll( int dx, int dy, int x, int y );

    //==== TitanVSP: move gizmo ====//
    // Arrows along X, Y and Z at the selected components' origin, as in Blender: drag one to move
    // them along that axis, in whole units.  Main window only.
    struct GizmoFrame
    {
        glm::vec2 m_Origin;         // Window pixels, from the bottom left.
        glm::vec2 m_Dir[3];         // Unit screen direction of each axis.
        float m_PxPerUnit[3];       // Screen pixels one model unit along each axis spans.
        bool m_Visible[3];          // Hidden when the axis points at the viewer.
    };
    bool GizmoShown();
    bool GizmoGetFrame( GizmoFrame & f );
    int GizmoHit( int x, int y );
    bool GizmoPush( int x, int y );
    void GizmoDrag( int x, int y );
    void GizmoRelease();
    void GizmoDraw();

    int m_GizmoAxis;                // Axis being dragged, or -1.
    int m_GizmoHover;               // Axis under the cursor, or -1.
    glm::vec2 m_GizmoStart;         // Where the drag started.
    glm::vec2 m_GizmoDir;           // The dragged axis on screen, and its pixels per unit.
    float m_GizmoPx;
    int m_GizmoSteps;               // Whole units moved so far.
    bool m_GizmoUndoSaved;          // The drag's starting place is on the undo stack.
    vector< string > m_GizmoGeoms;  // What's being moved, how, and from where.
    vector< bool > m_GizmoRel;
    vector< vec3d > m_GizmoStartLoc;
    vector< Matrix4d > m_GizmoAttachInv;

    //==== TitanVSP: click to select ====//
    // A left click -- pressed and released without dragging -- on a component selects it; with
    // Shift it's added to or taken out of the selection; on empty space it clears the selection.
    void ClickSelect( int x, int y, bool toggle );
    static void RefocusCB( void * data );   // Takes the keyboard back after a geom window opens.

    static void ViewAnimCB( void * data );
    bool m_ViewAnim = false;
    VSPGraphic::Camera * m_ViewAnimCam = nullptr;
    VSPGraphic::Camera::Pose m_ViewAnimFrom, m_ViewAnimTo;
    std::chrono::steady_clock::time_point m_ViewAnimStart;
    std::function< void() > m_ViewAnimDone;

    //==== TitanVSP: Blanks as balls ====//
    // A Blank has no surface, so it's drawn as a small shaded ball at its origin -- a fixed size on
    // screen -- with a ring round it when it's selected.  BlankHit finds the Blank whose ball is
    // under ( x, y ), for clicking.
    void BlankDraw( VSPGraphic::Viewport * vp );

    //==== TitanVSP: ground grid ====//
    // A grid on the XY plane under the model, fading out at 110% of the model's size.
    void GridDraw( VSPGraphic::Viewport * vp );

    //==== TitanVSP: keyboard move and rotate ====//
    // As in Blender: G (move) or R (rotate), then X, Y or Z, then a number, then Enter.  The axis
    // is drawn as a line through the selection, and the change previews as it's typed.  Esc or a
    // right click cancels; Enter or a left click applies.  No axis, no change.
    enum { XF_NONE, XF_MOVE, XF_ROTATE };
    bool XfKey();

    // TitanVSP: Blender's view keys, on the number row or keypad.  1 Front, 3 Left (nose to the
    // left), 7 Top, with Ctrl the opposite side (Rear, Right, Bottom); keypad 5 toggles perspective.
    bool ViewKey();
    void EndSnapOrtho();    // Orbiting leaves an axis view's orthographic (MainVSPScreen).
    void XfBegin( int mode );
    void XfApply( bool record );
    void XfRestore();
    void XfEnd( bool confirm );
    void XfDraw();
    void XfHintDraw();      // The G/R shortcuts, spelled out in the bottom left corner.

    int m_XfMode;
    int m_XfAxis;
    string m_XfText;
    bool m_XfSwallowKeyUp;
    vector< string > m_XfGeoms;
    vector< bool > m_XfRel;
    vector< vec3d > m_XfStartLoc;
    vector< vec3d > m_XfStartRot;
    vector< Matrix4d > m_XfAttachInv;
    string BlankHit( int x, int y );
    bool m_ClickCandidate;
    int m_ClickX, m_ClickY;

private:
    VSPGraphic::GraphicEngine * m_GEngine;

    struct TextureID
    {
        unsigned int bufferTexID;
        std::string geomTexID;
    };
    struct ID
    {
        unsigned int bufferID;

        std::string geomID;
        std::vector<TextureID> textureIDs;

        TextureID * find( const std::string &geomTexID )
        {
            for( int i = 0; i < ( int )textureIDs.size(); i++ )
            {
                if( textureIDs[i].geomTexID == geomTexID )
                {
                    return &textureIDs[i];
                }
            }
            return nullptr;
        }
    };
    std::unordered_map < std::string, ID > m_idMap;

    DrawObj::ScreenEnum m_LinkedScreen;

    bool m_initialized;

    // Boolean for setting default opengl screen size once
    bool m_hasSetSize;

    int m_prevViewport;

    glm::vec2 m_prevLB;
    glm::vec2 m_prevMB;
    glm::vec2 m_prevRB;
    glm::vec2 m_prevAltLB;
    glm::vec2 m_prevCtrlLB;
    glm::vec2 m_prevZLB;
    glm::vec2 m_prevMetaLB;
    glm::vec2 m_startShiftLB;
    glm::vec2 m_prevLBRB;

    unsigned m_prevKey;

protected:

    void _update( const std::vector<DrawObj*> & objects );
    void _updateColorScale( const std::vector<DrawObj*> & objects );

    ScreenMgr* m_ScreenMgr;
    int m_mouse_x;
    int m_mouse_y;
    bool m_noRotate; // Flag to enable/disable rotations

};

class EditXSecWindow : public VspGlWindow
{
public:

    EditXSecWindow( int x, int y, int w, int h, ScreenMgr* mgr );

    virtual int handle( int fl_event );

    virtual void update();

    void InitZoom(); // centers pan values and resets scale for zoom

private:

    // Search for a point index within radius 'r_test' to the input pick location
    int ihit( const vec3d & mpt, double r_test, const vec3d &scale );

    // Convert pixel in XSec editor to equivalent coordinate
    vec3d PixelToCoord( int x_pix, int y_pix );

    vector < vec3d > proj_pt_vec( const vector < vec3d > & pt_in );

    int m_LastHit; // Index of the previously selected point

};
}

#endif