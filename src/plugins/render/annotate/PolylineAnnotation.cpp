//
// This file is part of the Marble Virtual Globe.
//
// This program is free software licensed under the GNU LGPL. You can
// find a copy of this license in LICENSE.txt in the top directory of
// the source code.
//
// Copyright 2014      Calin Cruceru  <crucerucalincristian@gmail.com>
//

// Self
#include "PolylineAnnotation.h"

// Qt
#include <qmath.h>

// Marble
#include "SceneGraphicsTypes.h"
#include "GeoPainter.h"
#include "PolylineNode.h"
#include "MarbleColors.h"
#include "MarbleMath.h"
#include "GeoDataLineString.h"
#include "MergingNodesAnimation.h"
#include "GeoDataPlacemark.h"
#include "GeoDataTypes.h"
#include "ViewportParams.h"

#include <QDebug>

namespace Marble
{

const int PolylineAnnotation::regularDim = 15;
const int PolylineAnnotation::selectedDim = 15;
const int PolylineAnnotation::mergedDim = 20;
const int PolylineAnnotation::hoveredDim = 20;
const QColor PolylineAnnotation::regularColor = Oxygen::aluminumGray3;
const QColor PolylineAnnotation::selectedColor = Oxygen::aluminumGray6;
const QColor PolylineAnnotation::mergedColor = Oxygen::emeraldGreen6;
const QColor PolylineAnnotation::hoveredColor = Oxygen::grapeViolet6;


PolylineAnnotation::PolylineAnnotation( GeoDataPlacemark *placemark ) :
    SceneGraphicsItem( placemark ),
    m_viewport( 0 ),
    m_busy( false ),
    m_clickedNodeIndex( -1 ),
    m_hoveredNodeIndex( -1 ),
    m_virtualHoveredNode( -1 )

{
    // nothing to do
}

PolylineAnnotation::~PolylineAnnotation()
{
    delete m_animation;
}

void PolylineAnnotation::paint( GeoPainter *painter, const ViewportParams *viewport )
{
    m_viewport = viewport;
    Q_ASSERT( placemark()->geometry()->nodeType() == GeoDataTypes::GeoDataLineStringType );

    painter->save();
    if ( state() == SceneGraphicsItem::DrawingPolyline ) {
        setupRegionsLists( painter );
    } else {
        updateRegions( painter );
    }

    drawNodes( painter );
    painter->restore();
}

void PolylineAnnotation::setupRegionsLists( GeoPainter *painter )
{
    Q_ASSERT( state() == SceneGraphicsItem::DrawingPolyline );
    const GeoDataLineString line = static_cast<const GeoDataLineString>( *placemark()->geometry() );

    // Add poyline nodes.
    QVector<GeoDataCoordinates>::ConstIterator itBegin = line.begin();
    QVector<GeoDataCoordinates>::ConstIterator itEnd = line.end();

    m_nodesList.clear();
    for ( ; itBegin != itEnd; ++itBegin ) {
        PolylineNode newNode = PolylineNode( painter->regionFromEllipse( *itBegin, regularDim, regularDim ) );
        m_nodesList.append( newNode );
    }

    // Add region from polyline so that events on polyline's 'lines' could be caught.
    m_polylineRegion = painter->regionFromPolyline( line, 5 );
}

void PolylineAnnotation::updateRegions( GeoPainter *painter )
{
    if ( m_busy ) {
        return;
    }

    const GeoDataLineString line = static_cast<const GeoDataLineString>( *placemark()->geometry() );

    if ( state() == SceneGraphicsItem::MergingPolylineNodes ) {
        // Update the PolylineNodes lists after the animation has finished its execution.
        m_nodesList[m_secondMergedNode].setFlag( PolylineNode::NodeIsMergingHighlighted, false );
        m_hoveredNodeIndex = -1;

        // Remove the merging node flag and add the NodeIsSelected flag if either one of the
        // merged nodes had been selected before merging them.
        m_nodesList[m_secondMergedNode].setFlag( PolylineNode::NodeIsMerged, false );
        if ( m_nodesList[m_firstMergedNode].isSelected() ) {
            m_nodesList[m_secondMergedNode].setFlag( PolylineNode::NodeIsSelected );
        }
        m_nodesList.removeAt( m_firstMergedNode );

        m_firstMergedNode = -1;
        m_secondMergedNode = -1;
    } else if ( state() == SceneGraphicsItem::AddingPolylineNodes ) {
        // Create and update virtual nodes lists when being in the AddingPolgonNodes state, to
        // avoid overhead in other states.
        m_virtualNodesList.clear();
        QRegion firstRegion( painter->regionFromEllipse( line.at(0).interpolate( line.last(), 0.5 ),
                                                         hoveredDim, hoveredDim ) );
        m_virtualNodesList.append( PolylineNode( firstRegion ) );
        for ( int i = 0; i < line.size(); ++i ) {
            QRegion newRegion( painter->regionFromEllipse( line.at(i).interpolate( line.at(i+1), 0.5 ),
                                                           hoveredDim, hoveredDim ) );
            m_virtualNodesList.append( PolylineNode( newRegion ) );
        }
    }


    // Update the polyline region;
    m_polylineRegion = painter->regionFromPolyline( line, 5 );

    // Update the node lists.
    for ( int i = 0; i < m_nodesList.size(); ++i ) {
        QRegion newRegion;
        if ( m_nodesList.at(i).isSelected() ) {
            newRegion = painter->regionFromEllipse( line.at(i), selectedDim, selectedDim );
        } else {
            newRegion = painter->regionFromEllipse( line.at(i), regularDim, regularDim );
        }
        m_nodesList[i].setRegion( newRegion );
    }
}

void PolylineAnnotation::drawNodes( GeoPainter *painter )
{
    // These are the 'real' dimensions of the drawn nodes. The ones which have class scope are used
    // to generate the regions and they are a little bit larger, because, for example, it would be
    // a little bit too hard to select nodes.
    static const int d_regularDim = 10;
    static const int d_selectedDim = 10;
    static const int d_mergedDim = 20;
    static const int d_hoveredDim = 20;

    const GeoDataLineString line = static_cast<const GeoDataLineString>( *placemark()->geometry() );

    for ( int i = 0; i < line.size(); ++i ) {
        // The order here is important, because a merged node can be at the same time selected.
        if ( m_nodesList.at(i).isBeingMerged() ) {
            painter->setBrush( mergedColor );
            painter->drawEllipse( line.at(i), d_mergedDim, d_mergedDim );
        } else if ( m_nodesList.at(i).isSelected() ) {
            painter->setBrush( selectedColor );
            painter->drawEllipse( line.at(i), d_selectedDim, d_selectedDim );

            if ( m_nodesList.at(i).isEditingHighlighted() ||
                 m_nodesList.at(i).isMergingHighlighted() ) {
                QPen defaultPen = painter->pen();
                QPen newPen;
                newPen.setWidth( defaultPen.width() + 3 );

                if ( m_nodesList.at(i).isEditingHighlighted() ) {
                    newPen.setColor( QColor( 0, 255, 255, 120 ) );
                } else {
                    newPen.setColor( QColor( 25, 255, 25, 180 ) );
                }

                painter->setBrush( Qt::NoBrush );
                painter->setPen( newPen );
                painter->drawEllipse( line.at(i), d_selectedDim + 2, d_selectedDim + 2 );

                painter->setPen( defaultPen );
            }
        } else {
            painter->setBrush( regularColor );
            painter->drawEllipse( line.at(i), d_regularDim, d_regularDim );

            if ( m_nodesList.at(i).isEditingHighlighted() ||
                 m_nodesList.at(i).isMergingHighlighted() ) {
                QPen defaultPen = painter->pen();
                QPen newPen;
                newPen.setWidth( defaultPen.width() + 3 );

                if ( m_nodesList.at(i).isEditingHighlighted() ) {
                    newPen.setColor( QColor( 0, 255, 255, 120 ) );
                } else {
                    newPen.setColor( QColor( 25, 255, 25, 180 ) );
                }

                painter->setPen( newPen );
                painter->setBrush( Qt::NoBrush );
                painter->drawEllipse( line.at(i), d_regularDim + 2, d_regularDim + 2 );

                painter->setPen( defaultPen );
            }
        }
    }

    if ( m_virtualHoveredNode != -1 ) {
        painter->setBrush( hoveredColor );

        GeoDataCoordinates newCoords;
        if ( m_virtualHoveredNode ) {
            newCoords = line.at(m_virtualHoveredNode).interpolate( line.at(m_virtualHoveredNode-1), 0.5 );
        } else {
            newCoords = line.at(0).interpolate( line.last(), 0.5 );
        }
        painter->drawEllipse( newCoords, d_hoveredDim, d_hoveredDim );
    }
}

bool PolylineAnnotation::containsPoint( const QPoint &point ) const
{
    if ( state() == SceneGraphicsItem::Editing ) {
        return nodeContains( point ) != -1 || polylineContains( point );
    } else if ( state() == SceneGraphicsItem::MergingPolylineNodes ) {
        return nodeContains( point ) != -1;
    } else if ( state() == SceneGraphicsItem::AddingPolylineNodes ) {
        return virtualNodeContains( point ) != -1 ||
               nodeContains( point ) != -1 ||
               polylineContains( point );
    }

    return false;
}

int PolylineAnnotation::nodeContains( const QPoint &point ) const
{
    for ( int i = 0; i < m_nodesList.size(); ++i ) {
        if ( m_nodesList.at(i).containsPoint( point ) ) {
            return i;
        }
    }

    return -1;
}

int PolylineAnnotation::virtualNodeContains( const QPoint &point ) const
{
    for ( int i = 0; i < m_virtualNodesList.size(); ++i ) {
        if ( m_virtualNodesList.at(i).containsPoint( point ) )
            return i;
    }

    return -1;
}

bool PolylineAnnotation::polylineContains( const QPoint &point ) const
{
    return m_polylineRegion.contains( point );
}

void PolylineAnnotation::dealWithItemChange( const SceneGraphicsItem *other )
{
    Q_UNUSED( other );

    // So far we only deal with item changes when hovering nodes, so that
    // they do not remain hovered when changing the item we interact with.
    if ( state() == SceneGraphicsItem::Editing ) {
        if ( m_hoveredNodeIndex != -1 ) {
            m_nodesList[m_hoveredNodeIndex].setFlag( PolylineNode::NodeIsEditingHighlighted, false );
        }

        m_hoveredNodeIndex = -1;
    } else if ( state() == SceneGraphicsItem::MergingPolylineNodes ) {
        if ( m_hoveredNodeIndex != -1 ) {
            m_nodesList[m_hoveredNodeIndex].setFlag( PolylineNode::NodeIsMergingHighlighted, false );
        }

        m_hoveredNodeIndex = -1;
    } else if ( state() == SceneGraphicsItem::AddingPolylineNodes ) {
        m_virtualHoveredNode = -1;
    }
}

void PolylineAnnotation::move( const GeoDataCoordinates &source, const GeoDataCoordinates &destination )
{
    Q_UNUSED( source );
    Q_UNUSED( destination );
}

void PolylineAnnotation::setBusy( bool enabled )
{
    m_busy = enabled;

    if ( !enabled ) {
        delete m_animation;
    }
}

void PolylineAnnotation::deleteAllSelectedNodes()
{
    if ( state() != SceneGraphicsItem::Editing ) {
        return;
    }

    GeoDataLineString line = static_cast<GeoDataLineString>( *placemark()->geometry() );

    for ( int i = 0; i < line.size(); ++i ) {
        if ( m_nodesList.at(i).isSelected() ) {
            if ( m_nodesList.size() <= 3 ) {
                setRequest( SceneGraphicsItem::RemovePolylineRequest );
                return;
            }

            m_nodesList.removeAt( i );
            line.remove( i );
            --i;
        }
    }
}

void PolylineAnnotation::deleteClickedNode()
{
    if ( state() != SceneGraphicsItem::Editing ) {
        return;
    }

    GeoDataLineString line = static_cast<GeoDataLineString>( *placemark()->geometry() );
    if ( m_nodesList.size() <= 3 ) {
        setRequest( SceneGraphicsItem::RemovePolylineRequest );
        return;
    }

    m_nodesList.removeAt( m_clickedNodeIndex );
    line.remove( m_clickedNodeIndex );
 }

void PolylineAnnotation::changeClickedNodeSelection()
{
    if ( state() != SceneGraphicsItem::Editing ) {
        return;
    }

    m_nodesList[m_clickedNodeIndex].setFlag( PolylineNode::NodeIsSelected, false );
}

bool PolylineAnnotation::hasNodesSelected() const
{
    for ( int i = 0; i < m_nodesList.size(); ++i ) {
        if ( m_nodesList.at(i).isSelected() ) {
            return true;
        }
    }

    return false;
}

bool PolylineAnnotation::clickedNodeIsSelected() const
{
    return m_nodesList[m_clickedNodeIndex].isSelected();
}

QPointer<MergingNodesAnimation> PolylineAnnotation::animation()
{
    return m_animation;
}

bool PolylineAnnotation::mousePressEvent( QMouseEvent *event )
{
    if ( !m_viewport || m_busy ) {
        return false;
    }

    setRequest( SceneGraphicsItem::NoRequest );

    if ( state() == SceneGraphicsItem::Editing ) {
        return processEditingOnPress( event );
    } else if ( state() == SceneGraphicsItem::MergingPolylineNodes ) {
        return processMergingOnPress( event );
    } else if ( state() == SceneGraphicsItem::AddingPolylineNodes ) {
        return processAddingNodesOnPress( event );
    }

    return false;
}

bool PolylineAnnotation::mouseMoveEvent( QMouseEvent *event )
{
    if ( !m_viewport || m_busy ) {
        return false;
    }

    setRequest( SceneGraphicsItem::NoRequest );

    if ( state() == SceneGraphicsItem::Editing ) {
        return processEditingOnMove( event );
    } else if ( state() == SceneGraphicsItem::MergingPolylineNodes ) {
        return processMergingOnMove( event );
    } else if ( state() == SceneGraphicsItem::AddingPolylineNodes ) {
        return processAddingNodesOnMove( event );
    }

    return false;
}

bool PolylineAnnotation::mouseReleaseEvent( QMouseEvent *event )
{
    if ( !m_viewport || m_busy ) {
        return false;
    }

    setRequest( SceneGraphicsItem::NoRequest );

    if ( state() == SceneGraphicsItem::Editing ) {
        return processEditingOnRelease( event );
    } else if ( state() == SceneGraphicsItem::MergingPolylineNodes ) {
        return processMergingOnRelease( event );
    } else if ( state() == SceneGraphicsItem::AddingPolylineNodes ) {
        return processAddingNodesOnRelease( event );
    }

    return false;
}

void PolylineAnnotation::dealWithStateChange( SceneGraphicsItem::ActionState previousState )
{
    // Dealing with cases when exiting a state has an effect on this item.
    if ( previousState == SceneGraphicsItem::DrawingPolyline ) {
        // nothing so far
    } else if ( previousState == SceneGraphicsItem::Editing ) {
        // Make sure that when changing the state, there is no highlighted node.
        if ( m_hoveredNodeIndex != -1 ) {
            m_nodesList[m_hoveredNodeIndex].setFlag( PolylineNode::NodeIsEditingHighlighted, false );
        }

        m_clickedNodeIndex = -1;
        m_hoveredNodeIndex = -1;
    } else if ( previousState == SceneGraphicsItem::MergingPolylineNodes ) {
        // If there was only a node selected for being merged and the state changed,
        // deselect it.
        if ( m_firstMergedNode != -1 ) {
            m_nodesList[m_firstMergedNode].setFlag( PolylineNode::NodeIsMerged, false );
        }

        // Make sure that when changing the state, there is no highlighted node.
        if ( m_hoveredNodeIndex != -1 ) {
            if ( m_hoveredNodeIndex != -1 ) {
                m_nodesList[m_hoveredNodeIndex].setFlag( PolylineNode::NodeIsEditingHighlighted, false );
            }
        }

        m_hoveredNodeIndex = -1;
        delete m_animation;
    } else if ( previousState == SceneGraphicsItem::AddingPolylineNodes ) {
        m_virtualNodesList.clear();
        m_virtualHoveredNode = -1;
        m_adjustingNode = false;
    }

    // Dealing with cases when entering a state has an effect on this item, or
    // initializations are needed.
    if ( state() == SceneGraphicsItem::Editing ) {
        m_clickedNodeIndex = -1;
        m_hoveredNodeIndex = -1;
    } else if ( state() == SceneGraphicsItem::MergingPolylineNodes ) {
        m_firstMergedNode = -1;
        m_secondMergedNode = -1;
        m_hoveredNodeIndex = -1;
        m_animation = 0;
    } else if ( state() == SceneGraphicsItem::AddingPolylineNodes ) {
        m_virtualHoveredNode = -1;
        m_adjustingNode = false;
    }
}

bool PolylineAnnotation::processEditingOnPress( QMouseEvent *mouseEvent )
{
    if ( mouseEvent->button() != Qt::LeftButton && mouseEvent->button() != Qt::RightButton ) {
        return false;
    }

    qreal lat, lon;
    m_viewport->geoCoordinates( mouseEvent->pos().x(),
                                mouseEvent->pos().y(),
                                lon, lat,
                                GeoDataCoordinates::Radian );
    m_movedPointCoords.set( lon, lat );

    // First check if one of the nodes has been clicked.
    m_clickedNodeIndex = nodeContains( mouseEvent->pos() );
    if ( m_clickedNodeIndex != -1 ) {
        if ( mouseEvent->button() == Qt::RightButton ) {
            setRequest( SceneGraphicsItem::ShowNodeRmbMenu );
        }

        return true;
    }

    // Then check if the 'interior' of the polyline has been clicked (by interior
    // I mean its lines excepting its nodes).
    if ( polylineContains( mouseEvent->pos() ) ) {
        if ( mouseEvent->button() == Qt::RightButton ) {
            setRequest( SceneGraphicsItem::ShowPolylineRmbMenu );
        }

        return true;
    }

    return false;
}

bool PolylineAnnotation::processEditingOnMove( QMouseEvent *mouseEvent )
{
    if ( !m_viewport ) {
        return false;
    }

    qreal lon, lat;
    m_viewport->geoCoordinates( mouseEvent->pos().x(),
                                mouseEvent->pos().y(),
                                lon, lat,
                                GeoDataCoordinates::Radian );
    const GeoDataCoordinates newCoords( lon, lat );

    if ( m_clickedNodeIndex >= 0 ) {
        GeoDataLineString *line = static_cast<GeoDataLineString*>( placemark()->geometry() );
        line->at(m_clickedNodeIndex) = newCoords;

        return true;
    }

    return dealWithHovering( mouseEvent );
}

bool PolylineAnnotation::processEditingOnRelease( QMouseEvent *mouseEvent )
{
    static const int mouseMoveOffset = 1;

    if ( mouseEvent->button() != Qt::LeftButton ) {
        return false;
    }

    if ( m_clickedNodeIndex >= 0 ) {
        qreal x, y;

        m_viewport->screenCoordinates( m_movedPointCoords.longitude(),
                                       m_movedPointCoords.latitude(),
                                       x, y );
        // The node gets selected only if it is clicked and not moved.
        if ( qFabs(mouseEvent->pos().x() - x) > mouseMoveOffset ||
             qFabs(mouseEvent->pos().y() - y) > mouseMoveOffset ) {
            m_clickedNodeIndex = -1;
            return true;
        }

        m_nodesList[m_clickedNodeIndex].setFlag( PolylineNode::NodeIsSelected,
                                                 !m_nodesList.at(m_clickedNodeIndex).isSelected() );
        m_clickedNodeIndex = -1;
        return true;
    }

    return false;
}

bool PolylineAnnotation::processMergingOnPress( QMouseEvent *mouseEvent )
{
    if ( mouseEvent->button() != Qt::LeftButton ) {
        return false;
    }

    GeoDataLineString line = static_cast<GeoDataLineString>( *placemark()->geometry() );

    int index = nodeContains( mouseEvent->pos() );
    if ( index == -1 ) {
        return false;
    }

    // If this is the first node selected to be merged.
    if ( m_firstMergedNode == -1 ) {
        m_firstMergedNode = index;
        m_nodesList[index].setFlag( PolylineNode::NodeIsMerged );
   } else {
        Q_ASSERT( m_firstMergedNode != -1 );

        // Clicking two times the same node results in unmarking it for merging.
        if ( m_firstMergedNode == index ) {
            m_nodesList[index].setFlag( PolylineNode::NodeIsMerged, false );
            m_firstMergedNode = -1;
            return true;
        }

        // If these two nodes are the last ones remained as part of the polyline, remove
        // the whole polyline.
        if ( line.size() <= 3 ) {
            setRequest( SceneGraphicsItem::RemovePolylineRequest );
            return true;
        }
        m_nodesList[index].setFlag( PolylineNode::NodeIsMerged );
        m_secondMergedNode = -1;

        delete m_animation;
        // FIXME: m_animation = new MergingNodesAnimation( this );
        setRequest( SceneGraphicsItem::StartAnimation );
    }

    return true;
}

bool PolylineAnnotation::processMergingOnMove( QMouseEvent *mouseEvent )
{
    return dealWithHovering( mouseEvent );
}

bool PolylineAnnotation::processMergingOnRelease( QMouseEvent *mouseEvent )
{
    Q_UNUSED( mouseEvent );
    return true;
}

bool PolylineAnnotation::processAddingNodesOnPress( QMouseEvent *mouseEvent )
{
    if ( mouseEvent->button() != Qt::LeftButton ) {
        return false;
    }

    GeoDataLineString *line = static_cast<GeoDataLineString*>( placemark()->geometry() );

    // If a virtual node has just been clicked, add it to the polygon's outer boundary
    // and start 'adjusting' its position.
    int index = virtualNodeContains( mouseEvent->pos() );
    if ( index != -1 && !m_adjustingNode ) {
        Q_ASSERT( m_virtualHoveredNode == index );

        GeoDataLineString newLine( Tessellate );
        QList<PolylineNode> newList;
        for ( int i = index; i < index + line->size(); ++i ) {
            newLine.append( line->at(i % line->size()) );
            newList.append( PolylineNode( QRegion(), m_nodesList.at(i % line->size()).flags() ) );
        }
        newLine.append( newLine.at(0).interpolate( newLine.last(), 0.5 ) );

        m_nodesList = newList;
        m_nodesList.append( PolylineNode( QRegion() ) );
        *line = newLine;

        m_adjustingNode = true;
        m_virtualHoveredNode = -1;
        return true;
    }

    // If a virtual node which has been previously clicked and selected to become a
    // 'real node' is clicked one more time, it stops from being 'adjusted'.
    index = nodeContains( mouseEvent->pos() );
    if ( index != -1 && m_adjustingNode ) {
        m_adjustingNode = false;
        return true;
    }

    return false;
}

bool PolylineAnnotation::processAddingNodesOnMove( QMouseEvent *mouseEvent )
{
    Q_ASSERT( mouseEvent->button() == Qt::NoButton );

    int index = virtualNodeContains( mouseEvent->pos() );

    // If we are adjusting a virtual node which has just been clicked and became real, just
    // change its coordinates when moving it, as we do with nodes in Editing state on move.
    if ( m_adjustingNode ) {
        // The virtual node which has just been added is always the last within
        // GeoDataLinearRing's container.qreal lon, lat;
        qreal lon, lat;
        m_viewport->geoCoordinates( mouseEvent->pos().x(),
                                    mouseEvent->pos().y(),
                                    lon, lat,
                                    GeoDataCoordinates::Radian );
        const GeoDataCoordinates newCoords( lon, lat );
        GeoDataLineString line = static_cast<GeoDataLineString>( *placemark()->geometry() );
        line.last() = newCoords;

        return true;

    // If we are hovering a virtual node, store its index in order to be painted in drawNodes
    // method.
    } else if ( index != -1 ) {
        m_virtualHoveredNode = index;
        return true;
    }

    // This means that the interior of the polygon has been hovered. Let the event propagate
    // since there may be overlapping polygons.
    return false;
}

bool PolylineAnnotation::processAddingNodesOnRelease( QMouseEvent *mouseEvent )
{
    Q_UNUSED( mouseEvent );
    return !m_adjustingNode;
}

bool PolylineAnnotation::dealWithHovering( QMouseEvent *mouseEvent )
{
    PolylineNode::PolyNodeFlag flag = state() == SceneGraphicsItem::Editing ?
                                                    PolylineNode::NodeIsEditingHighlighted :
                                                    PolylineNode::NodeIsMergingHighlighted;

    int index = nodeContains( mouseEvent->pos() );
    if ( index != -1 ) {
        if ( !m_nodesList.at(index).isEditingHighlighted() &&
             !m_nodesList.at(index).isMergingHighlighted() ) {
            // Deal with the case when two nodes are very close to each other.
            if ( m_hoveredNodeIndex != -1 ) {
                m_nodesList[m_hoveredNodeIndex].setFlag( flag, false );
            }

            m_hoveredNodeIndex = index;
            m_nodesList[index].setFlag( flag );
        }

        return true;
    } else if ( m_hoveredNodeIndex != -1 ) {
        m_nodesList[m_hoveredNodeIndex].setFlag( flag, false );
        m_hoveredNodeIndex = -1;

        return true;
    }

    return false;
}

const char *PolylineAnnotation::graphicType() const
{
    return SceneGraphicsTypes::SceneGraphicPolyline;
}

}
