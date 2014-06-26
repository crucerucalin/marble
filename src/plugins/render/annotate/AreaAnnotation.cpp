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

// Self
#include "AreaAnnotation.h"

// Marble
#include "GeoDataPlacemark.h"
#include "GeoDataTypes.h"
#include "GeoPainter.h"
#include "ViewportParams.h"
#include "SceneGraphicsTypes.h"
#include "MarbleMath.h"
#include "GeoDataStyle.h"

// Qt
#include <qmath.h>

#include <QDebug>

namespace Marble
{

AreaAnnotation::AreaAnnotation( GeoDataPlacemark *placemark )
    : SceneGraphicsItem( placemark ),
      m_state( Normal ),
      m_style( 0 ),
      m_movedNodeIndex( -1 ),
      m_rightClickedNode( -2 ),
      m_viewport( 0 )
{

}

void AreaAnnotation::paint( GeoPainter *painter, const ViewportParams *viewport )
{
    m_viewport = viewport;
    QList<QRegion> regionList;

    painter->save();
    if ( placemark()->geometry()->nodeType() == GeoDataTypes::GeoDataPolygonType ) {
        const GeoDataPolygon *polygon = static_cast<const GeoDataPolygon*>( placemark()->geometry() );
        const GeoDataLinearRing &outerRing = polygon->outerBoundary();
        const QVector<GeoDataLinearRing> &innerRings = polygon->innerBoundaries();

        // First paint and add to the regions list the nodes which form the outerBoundary.
        for ( int i = 0; i < outerRing.size(); ++i ) {
            QRegion newRegion = painter->regionFromEllipse( outerRing.at(i), 15, 15 );

            // If the node is marked as selected, paint with a different color.
            if ( !m_selectedNodes.contains( i ) ) {
                painter->setBrush( Oxygen::aluminumGray3);
            } else {
                painter->setBrush( Oxygen::aluminumGray6 );
            }

            // If we are in the MergingNodes state, paint with a different color the node.
            if ( ( i == m_mergedNodes.first && m_state == MergingNodes ) ||
                 ( i == m_mergedNodes.second && m_state == MergingNodes ) ) {
                painter->setBrush( Oxygen::emeraldGreen6 );
                painter->drawEllipse( outerRing.at(i) , 15, 15 );
            } else {
                painter->drawEllipse( outerRing.at(i) , 10, 10 );
            }

            regionList.append( newRegion );
        }

        // Then paint and add to the regions list the nodes which form the innerBoundaries.
        int sizeOffset = outerRing.size();
        m_innerBoundariesList.clear();

        foreach ( const GeoDataLinearRing &ring, innerRings ) {
            for ( int i = 0; i < ring.size(); ++i ) {
                QRegion newRegion = painter->regionFromEllipse( ring.at(i), 15, 15 );

                if ( !m_selectedNodes.contains( i + sizeOffset ) ) {
                    painter->setBrush( Oxygen::aluminumGray3 );
                } else {
                    painter->setBrush( Oxygen::aluminumGray6 );
                }

                if ( ( i + sizeOffset == m_mergedNodes.first && m_state == MergingNodes ) ||
                     ( i + sizeOffset == m_mergedNodes.second && m_state == MergingNodes ) ) {
                    painter->setBrush( Oxygen::emeraldGreen6 );
                    painter->drawEllipse( ring.at(i) , 15, 15 );
                } else {
                    painter->drawEllipse( ring.at(i) , 10, 10 );
                }

                regionList.append( newRegion );
            }
            sizeOffset += ring.size();
            m_innerBoundariesList.append( painter->regionFromPolygon( ring, Qt::OddEvenFill ) );
        }

        // Add to the regions list the whole polygon.
        regionList.append( painter->regionFromPolygon( outerRing, Qt::OddEvenFill ) );

        // Add to the regionList the virtual nodes as well. As a consequence, the polygon's index
        // in the regions list will be regionList.size() - m_virtualNodesCount - 1.
        m_virtualNodesCount = 0;
        for ( int i = 0; i < outerRing.size(); ++i ) {
            if ( i ) {
                GeoDataCoordinates virtualNode = outerRing.at(i).interpolate( outerRing.at(i-1), 0.5 );
                regionList.append( painter->regionFromEllipse( virtualNode, 15, 15 ) );
            } else {
                GeoDataCoordinates virtualNode = outerRing.at(i).interpolate(
                                                       outerRing.at(outerRing.size() - 1), 0.5 );
                regionList.append( painter->regionFromEllipse( virtualNode, 15, 15 ) );
            }
            ++m_virtualNodesCount;
        }
    }

    painter->restore();
    setRegions( regionList );
}

bool AreaAnnotation::mousePressEvent( QMouseEvent *event )
{
    QList<QRegion> regionList = regions();

    qreal lat, lon;
    m_viewport->geoCoordinates( event->pos().x(), event->pos().y(),
                                lon, lat,
                                GeoDataCoordinates::Radian );
    m_movedPointCoords.set( lon, lat );

    int index = firstRegionWhichContains( event->pos() );
    int polyIndex = regionList.size() - m_virtualNodesCount - 1;

    // TODO: Clean this up.

    // If one of polygon's inner boundaries has been clicked, ignore the event.
    if ( index == polyIndex && isInnerBoundsPoint( event->pos() ) ) {
        m_rightClickedNode = -2;
        return false;
    }

    // This means that a virtual node has just been clicked.
    if ( index > polyIndex && m_state == AddingNodes ) {
        // problema e ca ia primul poligonul clickul. fuck
        m_style = 0;
        return true;
    } else if ( index > polyIndex ) {
        // In other states than AddingNodes, clicking the virtual nodes does nothing.
        return false;
    }

    if ( event->button() == Qt::LeftButton ) {
        m_movedNodeIndex = index;
        // If we are in the merging state store the clicked nodes.
        if ( m_state == MergingNodes && index < polyIndex ) {
            if ( m_mergedNodes.first != -1 && m_mergedNodes.second != -1 ) {
                m_mergedNodes = QPair<int, int>( -1, -1 );
            } else if ( m_mergedNodes.first == -1 ) {
                m_mergedNodes.first = index;
            } else {
                m_mergedNodes.second = index;
            }
        }

        return true;
    } else if ( event->button() == Qt::RightButton ) {
        if ( index < polyIndex ) {
            m_rightClickedNode = index;
        } else if ( index == polyIndex ) {
            m_rightClickedNode = -1;
        }

        return true;
    }

    return false;
}

bool AreaAnnotation::mouseMoveEvent( QMouseEvent *event )
{
    if ( !m_viewport ) {
        return false;
    }

    QList<QRegion> regionList = regions();
    int polyIndex = regionList.size() - m_virtualNodesCount - 1;

    // TODO: Clean this up.

    // We deal with polygon hovering only when being in AddingNodes state so far.
    if ( m_movedNodeIndex == -1 && m_state == AddingNodes ) {
        for ( int i = polyIndex + 1; i < regionList.size(); ++i ) {
            if ( regionList.at(i).contains( event->pos() ) ) {
                // daca deja e desenat, nu il mai desenez
                if ( m_style ) {
                    return true;
                }

                GeoDataStyle *style = new GeoDataStyle( *placemark()->style() );
                m_style = new GeoDataStyle( *style );

                style->lineStyle().setWidth( style->lineStyle().width() + 1 );
                placemark()->setStyle( style );

                return true;
            }
        }

        if ( m_style ) {
            placemark()->setStyle( m_style );
            m_style = 0;
            return false;
        }

        // If the hovered region is not one of polygon sides' middle points let the event
        // propagate.
        return false;
    // We don't have so far any functionality when hovering a polygon in other state, so let
    // the event propagate in this situation as well.
    } else if ( m_movedNodeIndex < 0 ) {
        return false;
    }

    qreal lon, lat;
    m_viewport->geoCoordinates( event->pos().x(),
                                event->pos().y(),
                                lon, lat,
                                GeoDataCoordinates::Radian );
    const GeoDataCoordinates coords( lon, lat );


    // This means one of the nodes has been clicked. The clicked node can be on the outer
    // boundary of the polygon as well as on its inner boundary.
    if ( m_movedNodeIndex >= 0 && m_movedNodeIndex < polyIndex ) {
        if ( placemark()->geometry()->nodeType() == GeoDataTypes::GeoDataPolygonType ) {
            GeoDataPolygon *polygon = static_cast<GeoDataPolygon*>( placemark()->geometry() );
            GeoDataLinearRing &outerRing = polygon->outerBoundary();

            // This means the clicked node is one of the nodes which form one of the
            // polygon's inner boundaries.
            if ( m_movedNodeIndex >= outerRing.size() ) {
                int newIndex = m_movedNodeIndex - outerRing.size();
                QVector<GeoDataLinearRing> &innerRings = polygon->innerBoundaries();

                for ( int i = 0; i < innerRings.size(); ++i ) {
                    if ( newIndex - innerRings.at(i).size() < 0 ) {
                        innerRings[i].at(newIndex) = coords;
                        break;
                    } else {
                        newIndex -= innerRings.at(i).size();
                    }
                }
            } else {
                outerRing[m_movedNodeIndex] = coords;
            }

            return true;
        } else {
            return false;
        }
    }

    // This means the interior of the polygon has been clicked (excepting its "holes" - its
    // inner boundaries) and here we handle the move of the entire polygon.
    Q_ASSERT( m_movedNodeIndex == polyIndex );
    qreal bearing = m_movedPointCoords.bearing( coords );
    qreal distance = distanceSphere( coords, m_movedPointCoords );

    if ( placemark()->geometry()->nodeType() == GeoDataTypes::GeoDataPolygonType ) {

        GeoDataPolygon *poly = static_cast<GeoDataPolygon*>( placemark()->geometry() );
        GeoDataLinearRing outerRing = poly->outerBoundary();
        QVector<GeoDataLinearRing> innerRings = poly->innerBoundaries();

        poly->outerBoundary().clear();
        poly->innerBoundaries().clear();

        for ( int i = 0; i < outerRing.size(); ++i ) {
            GeoDataCoordinates movedPoint = outerRing.at(i).moveByBearing( bearing, distance );
            qreal lon = movedPoint.longitude();
            qreal lat = movedPoint.latitude();

            GeoDataCoordinates::normalizeLonLat( lon, lat );
            movedPoint.setLongitude( lon );
            movedPoint.setLatitude( lat );

            poly->outerBoundary().append( movedPoint );
        }

        foreach ( const GeoDataLinearRing &ring, innerRings ) {
            GeoDataLinearRing newRing( Tessellate );
            for ( int i = 0; i < ring.size(); ++i ) {
                GeoDataCoordinates movedPoint = ring.at(i).moveByBearing( bearing, distance );
                qreal lon = movedPoint.longitude();
                qreal lat = movedPoint.latitude();

                GeoDataCoordinates::normalizeLonLat( lon, lat );
                movedPoint.setLongitude( lon );
                movedPoint.setLatitude( lat );

                newRing.append( movedPoint );
            }
            poly->innerBoundaries().append( newRing );
        }

        m_movedPointCoords.set( lon, lat );
        return true;
    }

    return false;
}

bool AreaAnnotation::mouseReleaseEvent( QMouseEvent *event )
{
    // The offset in pixels after which a mouse move event should not be considered actually a mouse
    // press event.
    static const int mouseMoveOffset = 1;

    // If the event is caught in one of the polygon's holes, we return false in
    // order to pass it to other potential polygons which have been drawn there.
    if ( isInnerBoundsPoint( event->pos() ) && m_movedNodeIndex == -1 ) {
        return false;
    }

    QList<QRegion> regionList = regions();

    m_movedNodeIndex = -1;
    m_rightClickedNode = -2;

    qreal x, y;
    m_viewport->screenCoordinates( m_movedPointCoords.longitude(), m_movedPointCoords.latitude(), x, y );

    // The node gets selected only if it is clicked and not moved.
    // Is this value ok in order to avoid this?
    if ( qFabs(event->pos().x() - x) > mouseMoveOffset ||
         qFabs(event->pos().y() - y) > mouseMoveOffset ) {
        return true;
    }

    // Get the index of the first region from the regionList which contains the event pos.
    // This may refer to a node or, if i == polyIndex, to the whole polygon.
    int index = firstRegionWhichContains( event->pos() );    
    int polyIndex = regionList.size() - m_virtualNodesCount - 1;

    // If the action state is set to MergingNodes then the clicked node should not get into the
    // selectedNodes list.
    if ( m_state == MergingNodes ) {
        return true;
    }

    // If this event has been caught on a node, add it to the selected nodes list if it has not
    // been selected already, or remove it otherwise.
    if ( index >= 0 && index < polyIndex && event->button() == Qt::LeftButton ) {
        if ( !m_selectedNodes.contains( index ) ) {
             m_selectedNodes.append( index );
        } else {
            m_selectedNodes.removeAll( index );
        }
    }

    // We return true even if we get here, because it means that there were no nodes to
    // be marked (the interior of the polygon has been clicked) and we don't want to do
    // anything else than release it, so we tell caller that we handled the event.
    return true;
}

void AreaAnnotation::setState( ActionState state )
{
    m_state = state;

    // TODO: clean this up
    // Do the initializations when entering a new state.
    switch ( state ) {
        case MergingNodes:
            m_mergedNodes = QPair<int, int>( -1, -1 );
            break;
        case AddingNodes:
            // nothing so far
            break;
        case Normal:
            if ( m_style ) {
                placemark()->setStyle( m_style );
                m_style = 0;
            }
            break;
    }
}

AreaAnnotation::ActionState AreaAnnotation::state() const
{
    return m_state;
}

QList<int> &AreaAnnotation::selectedNodes()
{
    return m_selectedNodes;
}

int AreaAnnotation::rightClickedNode() const
{
    return m_rightClickedNode;
}

bool AreaAnnotation::isInnerBoundsPoint( const QPoint &point, bool restrictive ) const
{
    foreach ( const QRegion &innerRegion, m_innerBoundariesList ) {
        if ( innerRegion.contains( point ) ) {
            if ( restrictive ) {
                QList<QRegion> regionList = regions();
                int polyIndex = regionList.size() - m_virtualNodesCount - 1;

                for ( int i = 0; i < polyIndex; ++i ) {
                    if ( regionList.at(i).contains( point ) ) {
                        return false;
                    }
                }

                return true;
            } else {
                return true;
            }
        }
    }

    return false;
}

bool AreaAnnotation::isValidPolygon() const
{
    const GeoDataPolygon *poly = static_cast<const GeoDataPolygon*>( placemark()->geometry() );

    foreach ( const GeoDataLinearRing &innerRing, poly->innerBoundaries() ) {
        for ( int i = 0; i < innerRing.size(); ++i ) {
            if ( !poly->outerBoundary().contains( innerRing.at(i) ) ) {
                return false;
            }
        }
    }

    return true;
}

void AreaAnnotation::setMergedNodes( const QPair<int, int> &nodes )
{
    m_mergedNodes = nodes;
}

QPair<int, int> &AreaAnnotation::mergedNodes()
{
    return m_mergedNodes;
}

const char *AreaAnnotation::graphicType() const
{
    return SceneGraphicTypes::SceneGraphicAreaAnnotation;
}

int AreaAnnotation::firstRegionWhichContains( const QPoint &eventPos ) const
{
    QList<QRegion> regionList = regions();

    for ( int i = 0; i < regionList.size(); ++i ) {
        if ( regionList.at(i).contains( eventPos ) ) {
            return i;
        }
    }

    // It cannot get here since the region list is used so far for handling the mouse events.
    Q_ASSERT( 0 );
    return -1;
}

}
