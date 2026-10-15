#ifndef _VSP_GRAPHIC_VIEWPORT_MANAGER_H
#define _VSP_GRAPHIC_VIEWPORT_MANAGER_H

#include <vector>
#include <functional>
#include "Common.h"

namespace VSPGraphic
{
class Scene;
class CameraMgr;
class Viewport;

/*!
* LayoutMgr class.
* Manager for Single or Multi-Viewport Display.
*/
class LayoutMgr
{
public:
    /*!
    * Constructor.
    * row and column define the numbers of viewports in screen.
    */
    LayoutMgr( int row, int column );
    /*!
    * Destructor.
    */
    virtual ~LayoutMgr();

public:
    /*!
    * Perform preprocessing. Color Picking.
    * x and y are current mouse location.
    */
    virtual void predraw( Scene * scene, int x, int y );

    /*!
    * Draw Scene.
    */
    virtual void draw( Scene * scene, int x, int y );

    /*!
    * Resize Screen.
    */
    virtual void resize( int width, int height );

    /*!
     * Resizing for screenshots only
     */
    virtual void resizeScreenshot( int width, int height, float screenSizeDiffRatio );

    /*
    * Select a Viewport base on the index of its order.
    *
    * Viewport order(left to right, up and down):
    * | 0 | 1 |
    * |-------|
    * | 2 | 3 |
    *
    * Set index to -1 to unselect viewports.
    */
    virtual void selectViewport( int index );

    /*
    * Select a Viewport base on mouse location.
    * If mouse location is outside viewports, unselect viewports.
    */
    virtual int selectViewport( int x, int y );

    /*
    * Get current selected Viewport.
    * If no viewport is selected, return nullptr pointer.
    */
    Viewport * getViewport();

public:
    /*
    * Get list of viewports in Manager.
    */
    std::vector<Viewport *> getViewports();

public:
    /*!
    * Return row of viewport in screen.
    */
    int row()
    {
        return _row;
    }

    /*!
    * Return column of viewport in screen.
    */
    int column()
    {
        return _column;
    }

    /*!
    * Return screen width in pixels.
    */
    int screenWidth()
    {
        return _sWidth;
    }

    /*!
    * Return screen height in pixels.
    */
    int screenHeight()
    {
        return _sHeight;
    }

    void setStartXY( int x, int y )
    {
        _startx = x;
        _starty = y;
    }

    /*!
    * The logo texture to draw behind each viewport, or 0 for none (TitanVSP).
    */
    /*!
    * Called for each viewport right after its scene is drawn, with its camera bound and its depth
    * buffer as the scene left it -- before the corner axes clear it (TitanVSP).
    */
    void setLineSmooth( bool smooth )
    {
        _lineSmooth = smooth;
    }

    void setSceneOverlay( std::function< void( Viewport * ) > overlay )
    {
        _sceneOverlay = overlay;
    }

    void setLogo( unsigned int texId, int width, int height )
    {
        _logoTex = texId;
        _logoW = width;
        _logoH = height;
    }

protected:
    std::vector<Viewport *> _viewportList;

    unsigned int _logoTex;
    int _logoW, _logoH;

    std::function< void( Viewport * ) > _sceneOverlay;
    bool _lineSmooth;

private:
    int _row;
    int _column;

    int _selected;

    int _sWidth;
    int _sHeight;

    int _startx, _starty;
};
}
#endif
