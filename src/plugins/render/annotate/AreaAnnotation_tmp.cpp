//
// This file is part of the Marble Virtual Globe.
//
// This program is free software licensed under the GNU LGPL. You can
// find a copy of this license in LICENSE.txt in the top directory of
// the source code.
//
// Copyright 2009      Andrew Manson            <g.real.ate@gmail.com>
// Copyright 2013      Thibaut Gridel           <tgridel@free.fr>
// Copyright 2014      Calin-Cristian Cruceru   <crucerucalincristian@gmail.com>
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
#include <QPair>


/**
 * NOTES:
 * -> metoda PAINT seteaza doar pentru prima data nodurile, cand se deseneaza poligonul, iar apoi eu ma
 *  ocup de acestea in mod dinamic, in timp ce modific poly->outerBoundary si innerBoundary schimb si
 *  listele de regiuni.
 * -> voi schimba modul in care se face inner boundary adding si nu voi mai desena nodurile in timp ce
 *  dau click ci le voi tine intr-un buffer, intr-un GeoDataLinearRing temporar pana cand iese din starea
 *  de adding polygon holes. Dupa, pur si simplu dau poly->innerBoundaries.append( buffer ). Face mult mai
 *  mult sens din punctul de vedere al utilizatorului.
 * -> modul in care e PAINT implementat in momentul de fata permite foarte usor extinderea pentru alte stari.
 *  Spre exemplu, pentru a face hover pe nodurile virtuale si pe inner boundaries trb sa adaug doar 2 foruri
 *  imbricate in drawNodes. INSA!! Eu acum, in implementare, trebuie sa ma asigur de urmatoarele lucruri:
 *      -> regiunile sunt conform cu outerboundary/inner boundary. Asta inseamna ca primul element din
 *  polygon->outerBoundary() corespunde regiunii pe care am creat-o pentru acesta in m_outerNodesList.
 *      -> Dupa fiecare operatie in functiile de event handling, trebuie ca nodurile sa aiba setate flagurile
 *  corecte. Spre exemplu, doar un nod la un moment dat din cele virtuale POATE avea flagul de virtual hovered.
 * -> in AnnotatePlugin sa am un approach de genul: pastrez ultimul SceneGraphicsItem cu care am interactionat
 *  si la fiecare event nou, verific daca itemul este altul. Daca este altul, apelez o functie care anunte toate
 *  itemele ca s-a schimbat itemul (sau poate pe cel vechi? pt moment). Asta ar fi folositoare, spre exemplu:
 *  in adding polygon holes, cand da click pe interiorul unui poligon handeluiesc aici (tin intr-un buffer, etc,
 *  am explicat mai sus), dar daca schimba poligonul pe care da click, cel vechi trebuie sa stie pentru a-l face
 *  real inner boundary-ul sau pentru a-l sterge daca are sub 3 noduri.
 */

namespace Marble {

class PolygonNode {

public:
    PolygonNode( QRegion region );
    ~PolygonNode();

    enum PolyNodeFlag {
        NoOption = 0x0,
        NodeIsSelected = 0x1,
        NodeIsInnerTmp = 0x2,
        NodeIsMerged = 0x4,
        NodeIsVirtualHovered = 0x8
    };

    Q_DECLARE_FLAGS(PolyNodeFlags, PolyNodeFlag)

    bool operator==( const PolygonNode &other ) const;
    bool operator!=( const PolygonNode &other ) const;

    bool isSelected() const;
    bool isInnerTmp() const;
    bool isBeingMerged() const;
    bool isVirtualHovered() const;

    void setFlag( PolyNodeFlag flag, bool enabled = true );
    void setFlags( PolyNodeFlags flags );
    void setRegion( QRegion newRegion );

    bool containsPoint( const QPoint &eventPos ) const;

private:
    QRegion m_region;
    PolyNodeFlags m_flags;
}

PolygonNode::PolygonNode( QRegion region ) :
    m_region( region ),
    m_flags( 0 )
{
    // nothing to do
}

PolygonNode::~PolygonNode()
{
    // nothing to do
}

bool PolygonNode::operator==( const PolygonNode &other ) const
{
    return m_region == other.m_region &&
           m_isSelected == other.m_isSelected &&
           m_isVirtual == other.m_isVirtual;
}

bool PolygonNode::operator!=( const PolygonNode &other ) const
{
    return !this->operator==( other );
}

bool PolygonNode::isSelected() const
{
    return m_flags & NodeIsSelected;
}

bool PolygonNode::isInnerTmp() const
{
    return m_flags & NodeIsInnerTmp;
}

bool PolygonNode::isBeingMerged() const
{
    return m_flags & NodeIsMerged;
}

bool PolygonNode::isVirtualHovered() const
{
    return m_flags & NodeIsVirtualHovered;
}

void PolygonNode::setRegion( QRegion newRegion )
{
    m_region = newRegion;
}

void PolygonNode::setFlag( PolyNodeFlag flag, bool enabled )
{
    if ( enabled ) {
        m_flags |= flag;
    } else {
        m_flags &= ~flag;
    }
}

void PolygonNode::setFlags( PolyNodeFlags flags )
{
    m_flags = flags;
}

bool PolygonNode::containsPoint( const QPoint &eventPos ) const
{
    return m_region.contains( eventPos );
}


const int AreaAnnotation::regularDim = 10;
const int AreaAnnotation::selectedDim = 10;
const int AreaAnnotation::mergedDim = 15;
const int AreaAnnotation::hoveredDim = 15;
const QColor AreaAnnotation::regularColor = Oxygen::aluminumGray3;
const QColor AreaAnnotation::selectedColor = Oxygen::aluminumGray6;
const QColor AreaAnnotation::mergedColor = Oxygen::emeraldGreen6;
const QColor AreaAnnotation::hoveredColor = Oxygen::burgundyPurple4;

AreaAnnotation::AreaAnnotation( GeoDataPlacemark *placemark ) :
    SceneGraphicsItem( placemark ),
    m_geopainter( 0 ),
    m_viewport( 0 ),
    m_regionsInitialized( false ),
    m_interactingObj( InteractingNothing )
{
    // nothing to do
}

AreaAnnotation::~AreaAnnotation()
{
    // nothing to do so far
}

void AreaAnnotation::paint( GeoPainter *painter, const ViewportParams *viewport )
{
    m_viewport = viewport;
    m_geopainter = painter;
    Q_ASSERT( placemark()->geometry()->nodeType() == GeoDataTypes::GeoDataPolygonType );

    painter->save();
    if ( !m_regionsInitialized ) {
        setupRegionsLists( painter );
        m_regionsInitialized = true;
    } else {
        updateBoundariesList();
    }

    drawNodes( painter );
    painter->restore();
}

bool AreaAnnotation::containsPoint( const QPoint &point ) const
{
    if ( state() == SceneGraphicsItem::Editing ) {
        return outerNodeContains( point ) != -1 || polygonContains( point ) ||
               innerNodeContains( point ) != QPair(-1, -1) ||

    } else if ( state() == SceneGraphicsItem::AddingPolygonHole ) {
        return polygonContains( point ) && outerNodeContains( point ) == -1 &&
               innerNodeContains( point ) == QPair(-1, -1);

    } else if ( state() == SceneGraphicsItem::MergingPolygonNodes ) {
        return outerNodeContains( point ) != -1 || innerNodeContains( point ) != QPair(-1, -1);

    } else if ( state() == SceneGraphicsItem::AddingNodes ) {
        return virtualNodeContains( point ) != -1;
    }

    return false;
}

void AreaAnnotation::itemChanged( const SceneGraphicItem &other )
{
    Q_UNUSED( other );

    if ( state() == SceneGraphicsItem::Editing ) {
        return;
    } else if ( state() == SceneGraphicsItem::AddingPolygonHole ) {
        // Check if a polygon hole was being drawn before moving to other item.
        GeoDataPolygon *polygon = static_cast<GeoDataPolygon*>( placemark()->geometry() );
        QVector<GeoDataLinearRing> &innerBounds = polygon->innerBoundaries();

        if ( innerBoundaries.size() && innerBounds.last().size() &&
             m_innerNodesList.last().last().isInnerTmp() ) {
            // If only two nodes were added, remove all this inner boundary.
            if ( innerBounds.last().size() <= 2 ) {
                innerBounds.remove( innerBoundaries.size() - 1 );
                m_innerNodesList.removeLast();
                return;
            }

            // Remove the 'NodeIsInnerTmp' flag, to allow ::draw method to paint the nodes.
            foreach ( const PolygonNode &node, m_innerNodesList.last() ) {
                node.setFlag( PolygonNode::NodeIsInnerTmp, false );
            }
        }
    } else if ( state() == SceneGraphicsItem::MergingPolygonNodes ) {

    } else if ( state() == SceneGraphicsItem::AddingNodes ) {

    }
}

bool AreaAnnotation::mousePressEvent( QMouseEvent *event )
{
    if ( !m_viewport || !m_geopainter ) {
        return false;
    }

    SceneGraphicsItem::ActionState state = state();

    if ( state == SceneGraphicsItem::Editing ) {
        return processEditingOnPress( event );
    } else if ( state == SceneGraphicsItem::AddingPolygonHole ) {
        return processAddingHoleOnPress( event );
    } else if ( state == SceneGraphicsItem::MergingPolygonNodes ) {

    } else if ( state == SceneGraphicsItem::AddingPolygonNodes ) {

    }

    return true;
}

bool AreaAnnotation::mouseMoveEvent( QMouseEvent *event )
{
    if ( !m_viewport || !m_geopainter ) {
        return false;
    }

    SceneGraphicsItem::ActionState state = state();

    if ( state == SceneGraphicsItem::Editing ) {
        return processEditingOnMove( event );
    } else if ( state == SceneGraphicsItem::AddingPolygonHole ) {
        return processAddingHoleOnMove( event );
    } else if ( state == SceneGraphicsItem::MergingPolygonNodes ) {

    } else if ( state == SceneGraphicsItem::AddingPolygonNodes ) {

    }

    return false;
}

bool AreaAnnotation::mouseReleaseEvent( QMouseEvent *event )
{
    if ( !m_viewport || !m_geopainter ) {
        return false;
    }

    SceneGraphicsItem::ActionState state = state();

    if ( state == SceneGraphicsItem::Editing ) {
        return processEditingOnMove( event );
    } else if ( state == SceneGraphicsItem::AddingPolygonHole ) {
        return processAddingHoleOnRelease( event );
    } else if ( state == SceneGraphicsItem::MergingPolygonNodes ) {
        return true;
    } else if ( state == SceneGraphicsItem::AddingPolygonNodes ) {
        return true;
    }

    return false;
}

void AreaAnnotation::stateChanged( SceneGraphicsItem::ActionState previousState )
{
    Q_UNUSED( previousState );

    if ( state() == SceneGraphicsItem::Editing ) {
        return;
    } else if ( state() == SceneGraphicsItem::AddingPolygonHole ) {
        // Check if a polygon hole was being drawn before moving to other item.
        GeoDataPolygon *polygon = static_cast<GeoDataPolygon*>( placemark()->geometry() );
        QVector<GeoDataLinearRing> &innerBounds = polygon->innerBoundaries();

        if ( innerBoundaries.size() && innerBounds.last().size() &&
             m_innerNodesList.last().last().isInnerTmp() ) {
            // If only two nodes were added, remove all this inner boundary.
            if ( innerBounds.last().size() <= 2 ) {
                innerBounds.remove( innerBoundaries.size() - 1 );
                m_innerNodesList.removeLast();
                return;
            }

            // Remove the 'NodeIsInnerTmp' flag, to allow ::draw method to paint the nodes.
            foreach ( const PolygonNode &node, m_innerNodesList.last() ) {
                node.setFlag( PolygonNode::NodeIsInnerTmp, false );
            }
        }
    } else if ( state() == SceneGraphicsItem::MergingPolygonNodes ) {

    } else if ( state() == SceneGraphicsItem::AddingNodes ) {

    }
}

const char *AreaAnnotation::graphicType() const
{
    return SceneGraphicTypes::SceneGraphicAreaAnnotation;
}

void AreaAnnotation::setupRegionLists( GeoPainter *painter )
{
    const static int regionDim = 20;

    const GeoDataPolygon *polygon = static_cast<const GeoDataPolygon*>( placemark()->geometry() );
    const GeoDataLinearRing &outerRing = polygon->outerBoundary();

    // Add the outer boundary nodes
    QVector<GeoDataCoordinates>::ConstIterator itBegin = outerRing.begin();
    QVector<GeoDataCoordinates>::ConstIterator itEnd = outerRing.end();

    for ( ; itBegin != itEnd; ++itBegin ) {
        PolygonNode newNode = PolygonNode( painter->regionFromEllipse( *itBegin, regionDim, regionDim ) );
        m_outerNodesList.append( newNode );
    }

    // Add the outer boundary to the boundaries list
    m_boundariesList.append( painter->regionFromPolygon( outerRing, Qt::OddEvenFill ) );
}

void AreaAnnotation::drawNodes( GeoPainter *painter )
{
    const GeoDataPolygon *polygon = static_cast<const GeoDataPolygon*>( placemark()->geometry() );
    const GeoDataLinearRing &outerRing = polygon->outerBoundary();
    const QVector<GeoDataLinearRing> &innerRings = polygon->innerBoundaries();

    for ( int i = 0; i < outerRing.size(); ++i ) {
        // The order here is important, because a merged node can be at the same time selected.
        if ( m_outerNodesList.at(i).isMerged() ) {
            painter->setBrush( mergedColor );
            painter->drawEllipse( outerRing.at(i), mergedDim, mergedDim );
        } else if ( m_outerNodesList.at(i).isSelected() ) {
            painter->setBrush( selectedColor );
            painter->drawEllipse( outerRing.at(i), selectedDim, selectedDim );
        } else {
            painter->setBrush( regularColor );
            painter->drawEllipse( outerRing.at(i), regularDim, regularDim );
        }
    }

    for ( int i = 0; i < innerRings.size(); ++i ) {
        for ( int j = 0; j < innerRings.at(i).size(); ++i ) {
            if ( m_innerNodesList.at(i).at(j).isMerged() ) {
                painter->setBrush( mergedColor );
                painter->drawEllipse( innerRings.at(i).at(j), mergedDim, mergedDim );
            } else if ( m_innerNodesList.at(i).at(j).isSelected() ) {
                painter->setBrush( selectedColor );
                painter->drawEllipse( innerRings.at(i).at(j), selectedDim, selectedDim );
            } else if ( m_innerNodesList.at(i).at(j).isInnerTmp() ) {
                // Do not draw inner nodes until the 'process' of adding these nodes ends
                // (aka while being in the 'Adding Polygon Hole').
                continue;
            } else {
                painter->setBrush( regularColor );
                painter->drawEllipse( innerRings.at(i).at(j), regularDim, regularDim );
            }
        }
    }

    for ( int i = 0; i < m_virtualNodesList.size(); ++i ) {
        if ( m_virtualNodesList.at(i).isVirtualHovered() ) {
            painter->setBrush( hoveredColor );

            GeoDataCoordinates coords;
            if ( i ) {
                coords = m_outerNodesList.at(i).interpolate( m_outerNodesList.at(i - 1), 0.5 );
            } else {
                coords = m_outerNodesList.at(i).interpolate(
                            m_outerNodesList.at(m_outerNodesList.size() - 1), 0.5 );
            }
            painter->drawEllipse( coords, hoveredDim, hoveredDim );
        }
    }
}

int AreaAnnotation::outerNodeContains( const QPoint &point ) const
{
    for ( int i = 0; i < m_outerNodesList.size(); ++i ) {
        if ( m_outerNodesList.at(i).containsPoint( point ) ) {
            return i;
        }
    }

    return -1;
}

QPair<int, int> AreaAnnotation::innerNodeContains( const QPoint &point ) const
{
    for ( int i = 0; i < m_innerNodesList.size(); ++i ) {
        for ( int j = 0; j < m_innerNodesList.at(i).size(); ++j ) {
            if ( m_innerNodesList.at(i).at(j).containsPoint( point ) ) {
                return QPair(i, j);
            }
        }
    }

    return QPair(-1, -1);
}

int AreaAnnotation::virtualNodeContains( const QPoint &point ) const
{
    for ( int i = 0; i < m_virtualNodesList.size(); ++i ) {
        if ( m_virtualNodesList.at(i).containsPoint( point ) ) {
            return i;
        }
    }

    return -1;
}

int AreaAnnotation::innerBoundsContain( const QPoint &point ) const
{
    for ( int i = 1; i < m_boundariesList.size(); ++i ) {
        if ( m_boundariesList.at(i).contains( point ) ) {
            return i;
        }
    }

    return -1;
}

bool AreaAnnotation::polygonContains( const QPoint &point ) const
{
    return m_boundariesList.at(0).contains( point ) && innerBoundsContain( point ) == -1;
}

bool AreaAnnotation::processEditingOnPress( QMouseEvent *mouseEvent )
{
    const GeoDataPolygon *polygon = static_cast<const GeoDataPolygon*>( placemark()->geometry() );
    const GeoDataLinearRing &outerRing = polygon->outerBoundary();
    const QVector<GeoDataLinearRing> &innerRings = polygon->innerBoundaries();

    if ( mouseEvent->button() != Qt::LeftButton && mouseEvent->button() != Qt::RightButton ) {
        return false;
    }

    // First check if one of the nodes from outer boundary has been clicked.
    int outerIndex = outerNodeContains( mouseEvent->pos() );
    if ( outerIndex != -1 ) {
        m_interactingObj = InteractingNode;
        m_clickedNodeIndexes = QPair( outerIndex, -1 );
        return true;
    }

    // Then check if one of the nodes which form an inner boundary has been clicked.
    QPair<int, int> innerIndexes = innerNodeContains( mouseEvent->pos() );
    if ( innerIndexes.first != -1 && innerIndexes.second != -1 ) {
        m_interactingObj = InteractingNode;
        m_clickedNodeIndexes = innerIndexes;
        return true;
    }

    // If neither outer boundary nodes nor inner boundary nodes contain the event position,
    // then check if the interior of the polygon (excepting its 'holes') contains this point.
    if ( polygonContains( mouseEvent->pos() ) ) {
        qreal lat, lon;
        m_viewport->geoCoordinates( mouseEvent->pos().x(),
                                    mouseEvent->pos().y(),
                                    lon, lat,
                                    GeoDataCoordinates::Radian );

        m_movedPointCoords.set( lon, lat );
        m_interactingObj = InteractingPolygon;
        return true;
    }

    // Normally, it should not get here, because in ::containsPoint() function, for the Editing
    // state we take into consideration exactly these three lists.
    Q_ASSERT( 0 );
    return false;
}

bool AreaAnnotation::processEditingOnMove( QMouseEvent *mouseEvent )
{
    // Mouse move events have always associated NoButton.
    Q_ASSERT( mouseEvent->button() == Qt::NoButton );

    qreal lon, lat;
    m_viewport->geoCoordinates( mouseEvent->pos().x(),
                                mouseEvent->pos().y(),
                                lon, lat,
                                GeoDataCoordinates::Radian );
    const GeoDataCoordinates newCoords( lon, lat );

    if ( m_interactingObj == InteractingNode ) {
        const GeoDataPolygon *polygon = static_cast<const GeoDataPolygon*>( placemark()->geometry() );
        const GeoDataLinearRing &outerRing = polygon->outerBoundary();
        const QVector<GeoDataLinearRing> &innerRings = polygon->innerBoundaries();

        int i = m_clickedNodeIndexes.first;
        int j = m_clickedNodeIndexes.second;

        if ( j == -1 ) {
            outerRing[i] = newCoords;

            QRegion newRegion;
            if ( m_outerNodesList.at(i).isSelected() ) {
                newRegion = m_geopainter->regionFromEllipse( newCoords, selectedDim, selectedDim );
            } else {
                newRegion = m_geopainter->regionFromEllipse( newCoords, regularDim, regularDim );
            }
            m_outerNodesList[i].setRegion( newRegion );
        } else {
            Q_ASSERT( i != -1 && j != -1 );
            innerRings[i].at(j) = newCoords;

            QRegion newRegion;
            if ( m_innerNodesList.at(i).at(j).isSelected() ) {
                newRegion = m_geopainter->regionFromEllipse( newCoords, selectedDim, selectedDim );
            } else {
                newRegion = m_geopainter->regionFromEllipse( newCoords, regularDim, regularDim );
            }
            m_innerNodesList.at(i).at(j).setRegion( newRegion );
        }

        return true;
    } else if ( m_interactingObj == InteractingPolygon ) {
        GeoDataPolygon *polygon = static_cast<GeoDataPolygon*>( placemark()->geometry() );
        GeoDataLinearRing outerRing = polygon->outerBoundary();
        QVector<GeoDataLinearRing> innerRings = polygon->innerBoundaries();

        const qreal bearing = m_movedPointCoords.bearing( newCoords );
        const qreal distance = distanceSphere( newCoords, m_movedPointCoords );

        polygon->outerBoundary().clear();
        polygon->innerBoundaries().clear();

        for ( int i = 0; i < outerRing.size(); ++i ) {
            GeoDataCoordinates movedPoint = outerRing.at(i).moveByBearing( bearing, distance );
            qreal lon = movedPoint.longitude();
            qreal lat = movedPoint.latitude();

            GeoDataCoordinates::normalizeLonLat( lon, lat );
            movedPoint.setLongitude( lon );
            movedPoint.setLatitude( lat );

            polygon->outerBoundary().append( movedPoint );

            QRegion newRegion;
            if ( m_outerNodesList.at(i).isSelected() ) {
                newRegion = m_geopainter->regionFromEllipse( movedPoint, selectedDim, selectedDim );
            } else {
                newRegion = m_geopainter->regionFromEllipse( movedPoint, regularDim, regularDim );
            }
            m_outerNodesList[i].setRegion( newRegion );
        }

        for ( int i = 0; i < innerRings.size(); ++i ) {
            GeoDataLinearRing newRing( Tessellate );
            for ( int j = 0; j < innerRings.at(i).size(); ++j ) {
                GeoDataCoordinates movedPoint = innerRings.at(i).at(j).moveByBearing( bearing, distance );
                qreal lon = movedPoint.longitude();
                qreal lat = movedPoint.latitude();

                GeoDataCoordinates::normalizeLonLat( lon, lat );
                movedPoint.setLongitude( lon );
                movedPoint.setLatitude( lat );

                newRing.append( movedPoint );

                QRegion newRegion;
                if ( m_innerNodesList.at(i).at(j).isSelected() ) {
                    newRegion = m_geopainter->regionFromEllipse( movedPoint, selectedDim, selectedDim );
                } else {
                    newRegion = m_geopainter->regionFromEllipse( movedPoint, regularDim, regularDim );
                }
                m_innerNodesList[i].at(j).setRegion = newRegion;
            }
            polygon->innerBoundaries().append( newRing );
        }

        m_movedPointCoords = newCoords;
        return true;
    } // Just need to add a new if ( m_interactingObj = InteractingNothing ) here if you one wants to
      // handle polygon hovers in Editing state.

    return false;
}

bool AreaAnnotation::processEditingOnRelease( QMouseEvent *mouseEvent )
{
    static const int mouseMoveOffset = 1;

    if ( mouseEvent->button() != Qt::LeftButton ) {
        return false;
    }

    if ( m_interactingObj == InteractingNode ) {
        qreal x, y;
        m_viewport->screenCoordinates( m_movedPointCoords.longitude(), m_movedPointCoords.latitude(), x, y );
        // The node gets selected only if it is clicked and not moved.
        if ( qFabs(mouseEvent->pos().x() - x) > mouseMoveOffset ||
             qFabs(mouseEvent->pos().y() - y) > mouseMoveOffset ) {
            return true;
        }

        if ( m_clickedNodeIndexes.second == -1 ) {
            m_outerNodesList[m_clickedNodeIndexes.first].setFlag( PolygonNode::NodeIsSelected );
        } else {
            m_innerNodesList[m_clickedNodeIndexes.first].at(m_clickedNodeIndexes.second).setFlag (
                                                                    PolygonNode::NodeIsSelected );
        }

        m_interactingObj = InteractingNothing;
        return true;
    } else if ( m_interactingObj == InteractingPolygon ) {
        // Nothing special happens at polygon release.
        m_interactingObj = InteractingNothing;
        return true;
    }

    // Normally, it should not get here, because all we can interact with so far are nodes and the
    // whole polygon.
    Q_ASSERT( 0 );
    return false;
}

bool AreaAnnotation::processAddingHoleOnPress( QMouseEvent *mouseEvent )
{
    // Due to the way ::containsPoint checks whether the mouse event position is contained by
    // the polygon when in this state, we can be sure that the click is within the interior of
    // the polygon
    if ( mouseEvent->button() != Qt::LeftButton ) {
        return false;
    }

    qreal lon, lat;
    m_marbleWidget->geoCoordinates( mouseEvent->pos().x(),
                                    mouseEvent->pos().y(),
                                    lon, lat,
                                    GeoDataCoordinates::Radian );
    const GeoDataCoordinates newCoords( lon, lat );

    GeoDataPolygon *polygon = static_cast<GeoDataPolygon*>( placemark()->geometry() );
    QVector<GeoDataLinearRing> &innerBounds = polygon->innerBoundaries();

    // Check if this is the first node which is being added as a new polygon inner boundary.
    if ( !innerBounds.size() || !m_innerNodesList.last().last().isInnerTmp() ) {
           polygon->innerBoundaries().append( GeoDataLinearRing( Tessellate ) );
           m_innerNodesList.append( QList<PolygonNode>() );
    }
    innerBounds.last().append( newCoords );


    PolygonNode newNode = PolygonNode( m_geopainter->regionFromEllipse( newCoords, regularDim, regularDim ) );
    newNode.setFlag( PolygonNode::NodeIsInnerTmp );
    m_innerNodesList.last().append( newNode );

    return true;
}

bool AreaAnnotation::processAddingHoleOnMove( QMouseEvent *mouseEvent )
{
    Q_UNUSED( mouseEvent );
    return true;
}

bool AreaAnnotation::processAddingHoleOnRelease( QMouseEvent *mouseEvent )
{
    Q_UNUSED( mouseEvent );
    return true;
}

bool AreaAnnotation::processMergingOnPress( QMouseEvent *mouseEvent )
{
    if ( mouseEvent->button() != Qt::RightButton ) {
        return false;
    }


}

bool AreaAnnotation::processMergingOnMove( QMouseEvent *mouseEvent )
{

}

bool AreaAnnotation::processMergingOnRelease( QMouseEvent *mouseEvent )
{

}

bool AreaAnnotation::processAddingNodesOnPress( QMouseEvent *mouseEvent )
{

}

bool AreaAnnotation::processAddingNodesOnMove( QMouseEvent *mouseEvent )
{

}

bool AreaAnnotation::processAddingNodesOnRelease( QMouseEvent *mouseEvent )
{

}

void AreaAnnotation::updateBoundariesList()
{
    const GeoDataPolygon *polygon = static_cast<const GeoDataPolygon*>( placemark()->geometry() );
    const GeoDataLinearRing &outerRing = polygon->outerBoundary();
    const QVector<GeoDataLinearRing> &innerRings = polygon->innerBoundaries();

    m_boundariesList.clear();

    m_boundariesList.append( m_geopainter->regionFromEllipse( outerRing, Qt::OddEvenFill ) );
    foreach ( const GeoDataLinearRing &ring, innerRings ) {
        m_boundariesList.append( m_geopainter->regionFromEllipse( ring, Qt::OddEvenFill ) );
    }
}


}

