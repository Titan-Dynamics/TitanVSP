#include "Camera.h"

#include "glm/ext.hpp"

#include <algorithm>
#include <cmath>

// Vertical field of view of the perspective camera, in degrees.  Narrow enough to look natural on
// an aircraft, wide enough to show depth.
#define PERSPECTIVE_FOV 30.0f

namespace VSPGraphic
{
Camera::Camera()
{

    _vx = _vy = 0;
    _vWidth = _vHeight = 1;

    _znear = -2500.0f;
    _zfar = 2500.0f;

    _oZoom = ORTHOGRAPHIC_ZOOM_INIT;
    _pZoom = PERSPECTIVE_ZOOM_INIT;

    _projectionType = Common::VSP_CAM_ORTHO;

    _projectionMatrix = glm::mat4( 1.0 );
    _modelviewMatrix = glm::mat4( 1.0 );
}
Camera::~Camera()
{
}

// In perspective the eye stands back from the centre of rotation, along the view direction, far
// enough that the plane through the centre of rotation fills the view just as the orthographic
// view does.  Rotating, panning and zooming are shared, so switching keeps the framing.
glm::mat4 Camera::getModelViewMatrix()
{
    if ( _projectionType == Common::VSP_CAM_PERSPECTIVE )
    {
        return glm::translate( glm::mat4( 1.0f ), glm::vec3( 0.0f, 0.0f, -_eyeDistance() ) ) * _modelviewMatrix;
    }
    return _modelviewMatrix;
}

glm::mat4 Camera::getOrbitMatrix()
{
    return _modelviewMatrix;
}

glm::mat4 Camera::getOrthoProjectionMatrix()
{
    float w = _vWidth / 2.0f;
    float h = _vHeight / 2.0f;
    return glm::ortho( -w * _oZoom, w * _oZoom, -h * _oZoom, h * _oZoom, _znear, _zfar );
}

float Camera::_eyeDistance()
{
    float h = _vHeight / 2.0f * _oZoom;
    return h / glm::tan( glm::radians( PERSPECTIVE_FOV / 2.0f ) );
}

Common::VSPenum Camera::getProjection()
{
    return _projectionType;
}

glm::mat4 Camera::getProjectionMatrix()
{
    return _projectionMatrix;
}

void Camera::resize( int x, int y, int width, int height )
{
    _vx = x;
    _vy = y;
    _vWidth = width;
    _vHeight = height;

    _calculateProjection();
}

void Camera::setZNearFar( float zn, float zf )
{
    _znear = zn;
    _zfar = zf;
    _calculateProjection();
}

void Camera::setProjection( Common::VSPenum type )
{
    _projectionType = type;
    _calculateProjection();
}

void Camera::changeView( Common::VSPenum type )
{
    switch( type )
    {
    case Common::VSP_CAM_TOP:
        setCOR( 0.0, 0.0, 0.0 );
        relativePan( 0.0, 0.0 );
        rotateSphere( 0, 0, 0 );
        break;

    case Common::VSP_CAM_FRONT:
        setCOR( 0.0, 0.0, 0.0 );
        relativePan( 0.0, 0.0 );
        rotateSphere( 0.0, - 90.0 * ( M_PI / 180.0 ), - 90.0 * ( M_PI / 180.0 ) );
        break;

    case Common::VSP_CAM_FRONT_YUP:
        setCOR( 0.0, 0.0, 0.0 );
        relativePan( 0.0, 0.0 );
        rotateSphere( 0.0, - 90.0 * ( M_PI / 180.0 ), 0.0 );
        break;

    case Common::VSP_CAM_LEFT:
        setCOR( 0.0, 0.0, 0.0 );
        relativePan( 0.0, 0.0 );
        rotateSphere( 90.0 * ( M_PI / 180.0 ), 0.0, 0.0 );
        break;

    case Common::VSP_CAM_LEFT_ISO:
        setCOR( 0.0, 0.0, 0.0 );
        relativePan( 0.0, 0.0 );
        rotateSphere( 0.1959132757121161 * M_PI, -0.25 * M_PI, -M_PI / 3.0 );
        break;

    case Common::VSP_CAM_BOTTOM:
        setCOR( 0.0, 0.0, 0.0 );
        relativePan( 0.0, 0.0 );
        rotateSphere( 0.0, 180.0 * ( M_PI / 180.0 ), 180.0 * ( M_PI / 180.0 ) );
        break;

    case Common::VSP_CAM_REAR:
        setCOR( 0.0, 0.0, 0.0 );
        relativePan( 0.0, 0.0 );
        rotateSphere( 0.0, 90.0 * ( M_PI / 180.0 ), 90.0 * ( M_PI / 180.0 ) );
        break;

    case Common::VSP_CAM_RIGHT:
        setCOR( 0.0, 0.0, 0.0 );
        relativePan( 0.0, 0.0 );
        rotateSphere( -90.0 * ( M_PI / 180.0 ), -180.0 * ( M_PI / 180.0 ), 0.0 );
        break;

    case Common::VSP_CAM_RIGHT_ISO:
        setCOR( 0.0, 0.0, 0.0 );
        relativePan( 0.0, 0.0 );
        rotateSphere( -0.1959132757121161 * M_PI, -0.25 * M_PI, -2.0 * M_PI / 3.0 );
        break;

    case Common::VSP_CAM_CENTER:
        center();
        break;

    default:
        break;
    }
}

Camera::Pose Camera::blendPose( const Pose & a, const Pose & b, float t )
{
    Pose p;

    // Slerp of unit quaternions, as 4-vectors.
    glm::vec4 qa = a.quat, qb = b.quat;
    float d = glm::dot( qa, qb );
    if ( d < 0.0f )
    {
        qb = -qb;
        d = -d;
    }
    if ( d > 0.9995f )
    {
        p.quat = glm::normalize( qa + ( qb - qa ) * t );
    }
    else
    {
        float th = acos( std::min( d, 1.0f ) );
        float s = sin( th );
        p.quat = qa * ( sin( ( 1.0f - t ) * th ) / s ) + qb * ( sin( t * th ) / s );
    }

    p.pan = a.pan + ( b.pan - a.pan ) * t;
    p.cor = a.cor + ( b.cor - a.cor ) * t;
    if ( a.zoom > 0.0f && b.zoom > 0.0f )
    {
        p.zoom = exp( log( a.zoom ) + ( log( b.zoom ) - log( a.zoom ) ) * t );
    }
    else
    {
        p.zoom = a.zoom + ( b.zoom - a.zoom ) * t;
    }
    return p;
}

void Camera::_calculateProjection()
{
    float w = _vWidth / 2.0;
    float h = _vHeight / 2.0;

    switch( _projectionType )
    {
    case Common::VSP_CAM_ORTHO:
        _leftB = -w * _oZoom;
        _rightB = w * _oZoom;
        _upB = -h * _oZoom;
        _downB = h * _oZoom;
        _projectionMatrix = glm::ortho( _leftB, _rightB, _upB, _downB, _znear, _zfar );
        break;

    case Common::VSP_CAM_PERSPECTIVE:
    {
        // The model reaches _zfar either side of the centre of rotation, which is the eye's
        // distance away.  The near plane stays well clear of the eye, so depth stays precise.
        float d = _eyeDistance();
        float zfar = d + std::abs( _zfar );
        float znear = std::max( d - std::abs( _zfar ), 0.001f * zfar );
        _projectionMatrix = glm::perspective( glm::radians( PERSPECTIVE_FOV ), ( float )_vWidth / ( float )_vHeight, znear, zfar );

        // Bounds of the view at the centre of rotation.
        _leftB = -w * _oZoom;
        _rightB = w * _oZoom;
        _upB = -h * _oZoom;
        _downB = h * _oZoom;
        break;
    }

    default:
        assert( false ); // Unknown Projection Type
        _leftB = -w * _oZoom;
        _rightB = w * _oZoom;
        _upB = -h * _oZoom;
        _downB = h * _oZoom;
        _projectionMatrix = glm::ortho( _leftB, _rightB, _upB, _downB, _znear, _zfar );
        break;
    }
}
}
