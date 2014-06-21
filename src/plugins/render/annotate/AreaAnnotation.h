//
// This file is part of the Marble Virtual Globe.
//
// This program is free software licensed under the GNU LGPL. You can
// find a copy of this license in LICENSE.txt in the top directory of
// the source code.
//
// Copyright 2009      Andrew Manson            <g.real.ate@gmail.com>
// Copyright 2013      Thibaut Gridel           <tgridel@free.fr>
// Copyright 2014      Calin-Cristian Cruceru   <crucerucalincristian@gmail.com
//

#ifndef AREAANNOTATION_H
#define AREAANNOTATION_H

#include "SceneGraphicsItem.h"
#include "GeoDataCoordinates.h"

#include <QPair>

namespace Marble
{

class AreaAnnotation : public SceneGraphicsItem
{
public:
    explicit AreaAnnotation( GeoDataPlacemark *placemark );

    enum ActionState {
        Normal,
        MergingNodes,
        AddingNodes
    };

    virtual void paint( GeoPainter *painter, const ViewportParams *viewport );

    void setState( ActionState state );

    ActionState state() const;

    /**
     * @brief Returns the list of selected node indexes.
     */
    QList<int> &selectedNodes();

    /**
     * @brief Returns the node index on which the mouse press event (with the right
     * button) has been caught.
     */
    int rightClickedNode() const;

    /**
     * @brief Checks whether the point parameter is contained by one of its inner
     * boundaries.
     * @p restrictive If this parameter is set to false, only check if one of its
     * inner boundaries contains the point (using GeoDataLinerRing::contains). In
     * addition to this, when restrictive is set to true, also check that none of
     * the polygon's regions (its nodes) contain the point (yes, these regions may
     * 'intersect' due to the way nodes are represented).
     */
    bool isInnerBoundsPoint( const QPoint &point, bool restrictive = false ) const;

    /**
     * @brief Checks if the polygon has a valid shape; an invalid shape would be, for
     * example, if one of its inner boundaries ring is intersected by its outer
     * boundary ring.
     */
    bool isValidPolygon() const;

    void setMergedNodes( const QPair<int, int> &nodes );

    QPair<int, int> &mergedNodes();

    const QPair<int, int> &mergedNodes() const;

    virtual const char *graphicType() const;

private:
    QList<QRegion>     m_innerBoundariesList;
    ActionState        m_state;

    QPair<int, int>    m_mergedNodes;

    int                m_movedNodeIndex;
    int                m_rightClickedNode;
    QList<int>         m_selectedNodes;
    GeoDataCoordinates m_movedPointCoords;

    const ViewportParams *m_viewport;

protected:
    /**
     * @brief In the implementation of these virtual functions, the following convention has  been
     * followed: if the event cannot be dealt with in this class (for example when right clicking
     * a node or polygon), the functions return false and AnnotatePlugin::eventFilter deals with it.
     */
    virtual bool mousePressEvent( QMouseEvent *event );
    virtual bool mouseMoveEvent( QMouseEvent *event );
    virtual bool mouseReleaseEvent( QMouseEvent *event );
};

}

#endif // AREAANNOTATION_H
