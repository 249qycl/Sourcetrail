#include "qt/graphics/QtGraphicsView.h"

#include <QDir>
#include <QMouseEvent>
#include <QScrollBar>
#include <QTimer>

#include <QApplication>
#include <QClipboard>
#include <QPropertyAnimation>
#include <QParallelAnimationGroup>
#include <QSvgGenerator>

#include "qt/element/QtIconButton.h"
#include "qt/view/graphElements/QtGraphEdge.h"
#include "qt/view/graphElements/QtGraphNode.h"
#include "qt/view/graphElements/QtGraphNodeData.h"
#include "qt/view/graphElements/QtGraphNodeBundle.h"
#include "qt/utility/QtContextMenu.h"
#include "qt/utility/QtFileDialog.h"
#include "qt/utility/utilityQt.h"
#include "settings/ApplicationSettings.h"
#include "utility/messaging/type/code/MessageCodeShowDefinition.h"
#include "utility/messaging/type/MessageDisplayBookmarkCreator.h"
#include "utility/messaging/type/MessageGraphNodeHide.h"
#include "utility/ResourcePaths.h"
#include "utility/utilityApp.h"

QtGraphicsView::QtGraphicsView(QWidget* parent)
	: QGraphicsView(parent)
	, m_zoomFactor(1.0f)
	, m_appZoomFactor(1.0f)
	, m_up(false)
	, m_down(false)
	, m_left(false)
	, m_right(false)
	, m_shift(false)
	, m_zoomInButtonSpeed(20.0f)
	, m_zoomOutButtonSpeed(-20.0f)
{
	QString modifierName = utility::getOsType() == OS_MAC ? "Cmd" : "Ctrl";

	setTransformationAnchor(QGraphicsView::AnchorUnderMouse);

	m_timer = std::make_shared<QTimer>(this);
	connect(m_timer.get(), &QTimer::timeout, this, &QtGraphicsView::updateTimer);

	m_timerStopper = std::make_shared<QTimer>(this);
	m_timerStopper->setSingleShot(true);
	connect(m_timerStopper.get(), &QTimer::timeout, this, &QtGraphicsView::stopTimer);

	m_zoomLabelTimer = std::make_shared<QTimer>(this);
	connect(m_zoomLabelTimer.get(), &QTimer::timeout, this, &QtGraphicsView::hideZoomLabel);

	m_exportGraphAction = new QAction("Save as Image", this);
	m_exportGraphAction->setStatusTip("Save this graph as image file");
	m_exportGraphAction->setToolTip("Save this graph as image file");
	connect(m_exportGraphAction, &QAction::triggered, this, &QtGraphicsView::exportGraph);

	m_copyNodeNameAction = new QAction("Copy Name", this);
	m_copyNodeNameAction->setStatusTip("Copies the name of this node to the clipboard");
	m_copyNodeNameAction->setToolTip("Copies the name of this node to the clipboard");
	connect(m_copyNodeNameAction, &QAction::triggered, this, &QtGraphicsView::copyNodeName);

	m_showDefinitionAction = new QAction("Show Definition (Ctrl + Left Click)", this);
#if defined(Q_OS_MAC)
	m_showDefinitionAction->setText("Show Definition (Cmd + Left Click)");
#endif
	m_showDefinitionAction->setStatusTip("Show definition of this symbol in the code");
	m_showDefinitionAction->setToolTip("Show definition of this symbol in the code");
	connect(m_showDefinitionAction, &QAction::triggered, this, &QtGraphicsView::showDefinition);

	m_hideNodeAction = new QAction("Hide Node (Alt + Left Click)", this);
	m_hideNodeAction->setStatusTip("Hide the node from this graph");
	m_hideNodeAction->setToolTip("Hide the node from this graph");
	connect(m_hideNodeAction, &QAction::triggered, this, &QtGraphicsView::hideNode);

	m_hideEdgeAction = new QAction("Hide Edge (Alt + Left Click)", this);
	m_hideEdgeAction->setStatusTip("Hide the edge from this graph");
	m_hideEdgeAction->setToolTip("Hide the edge from this graph");
	connect(m_hideEdgeAction, &QAction::triggered, this, &QtGraphicsView::hideEdge);

	m_bookmarkNodeAction = new QAction("Bookmark Node", this);
	m_bookmarkNodeAction->setStatusTip("Create a bookmark for this node");
	m_bookmarkNodeAction->setToolTip("Create a bookmark for this node");
	connect(m_bookmarkNodeAction, &QAction::triggered, this, &QtGraphicsView::bookmarkNode);

	m_zoomState = new QPushButton(this);
	m_zoomState->setObjectName("zoom_state");
	m_zoomState->hide();

	m_zoomInButton = new QtSelfRefreshIconButton(
		"", ResourcePaths::getGuiPath().concatenate(L"graph_view/images/zoom_in.png"), "search/button", this);
	m_zoomInButton->setObjectName("zoom_in_button");
	m_zoomInButton->setAutoRepeat(true);
	m_zoomInButton->setToolTip("Zoom in (" + modifierName + " + Mousewheel forward)");
	connect(m_zoomInButton, &QPushButton::pressed, this, &QtGraphicsView::zoomInPressed);

	m_zoomOutButton = new QtSelfRefreshIconButton(
		"", ResourcePaths::getGuiPath().concatenate(L"graph_view/images/zoom_out.png"), "search/button", this);
	m_zoomOutButton->setObjectName("zoom_out_button");
	m_zoomOutButton->setAutoRepeat(true);
	m_zoomOutButton->setToolTip("Zoom out (" + modifierName + " + Mousewheel back)");
	connect(m_zoomOutButton, &QPushButton::pressed, this, &QtGraphicsView::zoomOutPressed);
}

float QtGraphicsView::getZoomFactor() const
{
	return m_appZoomFactor * m_zoomFactor;
}

void QtGraphicsView::setAppZoomFactor(float appZoomFactor)
{
	m_appZoomFactor = appZoomFactor;
	updateTransform();
}

void QtGraphicsView::setSceneRect(const QRectF& rect)
{
	QGraphicsView::setSceneRect(rect);
	scene()->setSceneRect(rect);
}

QtGraphNode* QtGraphicsView::getNodeAtCursorPosition() const
{
	QtGraphNode* node = nullptr;

	QPointF point = mapToScene(mapFromGlobal(QCursor::pos()));
	QGraphicsItem* item = scene()->itemAt(point, QTransform());
	if (item)
	{
		node = dynamic_cast<QtGraphNode*>(item->parentItem());
	}

	return node;
}

QtGraphEdge* QtGraphicsView::getEdgeAtCursorPosition() const
{
	QtGraphEdge* edge = nullptr;

	QPointF point = mapToScene(mapFromGlobal(QCursor::pos()));
	QGraphicsItem* item = scene()->itemAt(point, QTransform());
	if (item)
	{
		edge = dynamic_cast<QtGraphEdge*>(item->parentItem());
	}

	return edge;
}

void QtGraphicsView::ensureVisibleAnimated(const QRectF& rect, int xmargin, int ymargin)
{
	int xval = horizontalScrollBar()->value();
	int yval = verticalScrollBar()->value();

	ensureVisible(rect, xmargin, ymargin);

	if (ApplicationSettings::getInstance()->getUseAnimations())
	{
		int xval2 = horizontalScrollBar()->value();
		int yval2 = verticalScrollBar()->value();

		horizontalScrollBar()->setValue(xval);
		verticalScrollBar()->setValue(yval);

		QParallelAnimationGroup* move = new QParallelAnimationGroup();

		QPropertyAnimation* xanim = new QPropertyAnimation(horizontalScrollBar(), "value");
		xanim->setDuration(150);
		xanim->setStartValue(xval);
		xanim->setEndValue(xval2);
		xanim->setEasingCurve(QEasingCurve::InOutQuad);
		move->addAnimation(xanim);

		QPropertyAnimation* yanim = new QPropertyAnimation(verticalScrollBar(), "value");
		yanim->setDuration(150);
		yanim->setStartValue(yval);
		yanim->setEndValue(yval2);
		yanim->setEasingCurve(QEasingCurve::InOutQuad);
		move->addAnimation(yanim);

		move->start();
	}
}

void QtGraphicsView::updateZoom(float delta)
{
	float factor = 1.0f + 0.001f * delta;

	if (factor <= 0.0f)
	{
		factor = 0.000001;
	}

	double newZoom = m_zoomFactor * factor;
	setZoomFactor(qBound(0.1, newZoom, 100.0));
}

void QtGraphicsView::resizeEvent(QResizeEvent* event)
{
	m_zoomState->setGeometry(QRect(31, event->size().height() - 27, 65, 19));
	m_zoomInButton->setGeometry(QRect(8, event->size().height() - 50, 19, 19));
	m_zoomOutButton->setGeometry(QRect(8, event->size().height() - 27, 19, 19));

	m_zoomInButton->setIconSize(QSize(15, 15));
	m_zoomOutButton->setIconSize(QSize(15, 15));

	emit resized();
}

void QtGraphicsView::mousePressEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton && !itemAt(event->pos()))
	{
		m_last = event->pos();
	}

	QGraphicsView::mousePressEvent(event);
}

void QtGraphicsView::mouseReleaseEvent(QMouseEvent *event)
{
	if (event->button() == Qt::LeftButton && !itemAt(event->pos()) && event->pos() == m_last)
	{
		emit emptySpaceClicked();
	}

	QGraphicsView::mouseReleaseEvent(event);
	viewport()->setCursor(Qt::ArrowCursor);
}

void QtGraphicsView::keyPressEvent(QKeyEvent* event)
{
	if (event->key() >= Qt::Key_A && event->key() <= Qt::Key_Z && event->text().size())
	{
		QChar c = event->text().at(0).toUpper();
		emit characterKeyPressed(c);
	}

	bool moved = moves();

	switch (event->key())
	{
		case Qt::Key_W:
			m_up = true;
			break;
		case Qt::Key_A:
			m_left = true;
			break;
		case Qt::Key_S:
			m_down = true;
			break;
		case Qt::Key_D:
			m_right = true;
			break;
		case Qt::Key_0:
			setZoomFactor(1.0f);
			updateTransform();
			break;
		case Qt::Key_Shift:
			m_shift = true;
			break;
		default:
			QGraphicsView::keyPressEvent(event);
			return;
	}

	if (!moved && moves())
	{
		m_timer->start(20);
	}

	m_timerStopper->start(1000);
}

void QtGraphicsView::keyReleaseEvent(QKeyEvent* event)
{
	switch (event->key())
	{
		case Qt::Key_W:
			m_up = false;
			break;
		case Qt::Key_A:
			m_left = false;
			break;
		case Qt::Key_S:
			m_down = false;
			break;
		case Qt::Key_D:
			m_right = false;
			break;
		case Qt::Key_Shift:
			m_shift = false;
			break;
		default:
			return;
	}

	if (!moves())
	{
		m_timer->stop();
	}
}

void QtGraphicsView::wheelEvent(QWheelEvent* event)
{
	bool zoomDefault = ApplicationSettings::getInstance()->getControlsGraphZoomOnMouseWheel();
	bool shiftPressed = event->modifiers() == Qt::ShiftModifier;
	bool ctrlPressed = event->modifiers() == Qt::ControlModifier;

	if (zoomDefault != (shiftPressed | ctrlPressed))
	{
		if (event->delta() != 0.0f)
		{
			updateZoom(event->delta());
		}
	}
	else
	{
		QGraphicsView::wheelEvent(event);
	}
}

void QtGraphicsView::contextMenuEvent(QContextMenuEvent* event)
{
	m_clipboardNodeName = L"";
	m_hideNodeId = 0;
	m_hideEdgeId = 0;
	m_bookmarkNodeId = 0;
	FilePath clipboardFilePath;

	QtGraphNode* node = getNodeAtCursorPosition();
	if (node)
	{
		while (node)
		{
			m_hideNodeId = node->getTokenId();

			QtGraphNodeData* dataNode = dynamic_cast<QtGraphNodeData*>(node);
			if (dataNode)
			{
				m_clipboardNodeName = dataNode->getName();
				m_bookmarkNodeId = dataNode->getTokenId();
				clipboardFilePath = dataNode->getFilePath();
				break;
			}
			else if (dynamic_cast<QtGraphNodeBundle*>(node))
			{
				m_clipboardNodeName = node->getName();
				break;
			}

			node = node->getParent();
		}
	}
	else
	{
		QtGraphEdge* edge = getEdgeAtCursorPosition();
		if (edge)
		{
			m_hideEdgeId = edge->getTokenId();
		}
	}

	m_showDefinitionAction->setEnabled(m_hideNodeId);
	m_hideNodeAction->setEnabled(m_hideNodeId);
	m_hideEdgeAction->setEnabled(m_hideEdgeId);
	m_bookmarkNodeAction->setEnabled(m_bookmarkNodeId);

	m_copyNodeNameAction->setEnabled(!m_clipboardNodeName.empty());

	QtContextMenu menu(event, this);

	menu.addSeparator();
	menu.addAction(m_exportGraphAction);

	menu.addSeparator();
	menu.addAction(m_showDefinitionAction);
	menu.addAction(m_hideNodeAction);
	menu.addAction(m_hideEdgeAction);
	menu.addAction(m_bookmarkNodeAction);

	menu.addSeparator();
	menu.addAction(m_copyNodeNameAction);
	menu.addFileActions(clipboardFilePath);

	menu.show();
}

void QtGraphicsView::updateTimer()
{
	float ds = 30.0f;
	float dz = 50.0f;

	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;

	if (m_shift)
	{
		if (m_up)
		{
			z += dz;
		}
		else if (m_down)
		{
			z -= dz;
		}
	}
	else
	{
		if (m_up)
		{
			y -= ds;
		}
		else if (m_down)
		{
			y += ds;
		}

		if (m_left)
		{
			x -= ds;
		}
		else if (m_right)
		{
			x += ds;
		}
	}

	if (x != 0)
	{
		horizontalScrollBar()->setValue(horizontalScrollBar()->value() + x);
	}

	if (y != 0)
	{
		verticalScrollBar()->setValue(verticalScrollBar()->value() + y);
	}

	if (z != 0)
	{
		updateZoom(z);
	}
}

void QtGraphicsView::stopTimer()
{
	m_timer->stop();
}

void QtGraphicsView::exportGraph()
{
	const QString exportNotice = "Exported from Sourcetrail";
	const int margin = 10;

	FilePath filePath(QtFileDialog::showSaveFileDialog(
		nullptr, "Save image", FilePath(), "PNG (*.png);;JPEG (*.JPEG);;BMP Files (*.bmp);;SVG (*.svg)"
	).toStdWString());


	if (filePath.extension() == L".svg")
	{
		QSvgGenerator svgGen;
		svgGen.setFileName(QString::fromStdWString(filePath.wstr()));
		svgGen.setSize(scene()->sceneRect().size().toSize());
		svgGen.setViewBox(QRect(QPoint(0, 0), scene()->sceneRect().size().toSize()));
		svgGen.setTitle(QString::fromStdWString(filePath.withoutExtension().fileName()));
		svgGen.setDescription(QString("Graph exported from Sourcetrail") + QChar(0x00AE));

		QPainter painter(&svgGen);
		scene()->render(&painter);

		{
			QFont font("Fira Sans, sans-serif");
			font.setPixelSize(8);
			painter.setFont(font);
		}
		{
			QRect boundingRect;
			painter.drawText(
				QRect(margin, margin, svgGen.size().width() - 2 * margin, svgGen.size().height() - 2 * margin),
				Qt::AlignBottom | Qt::AlignHCenter,
				exportNotice + ' ' + QChar(0x00AE),
				&boundingRect
			);
		}
	}
	else if (!filePath.empty())
	{
		QImage image(scene()->sceneRect().size().toSize() * 2, QImage::Format_ARGB32);
		image.fill(Qt::transparent);

		QPainter painter(&image);
		painter.setRenderHint(QPainter::Antialiasing);
		scene()->render(&painter);

		{
			QFont font = painter.font();
			font.setPixelSize(14);
			painter.setFont(font);
		}
		{
			QRect boundingRect;
			painter.drawText(
				QRect(margin, margin, image.size().width() - 2 * margin, image.size().height() - 2 * margin),
				Qt::AlignBottom | Qt::AlignHCenter,
				exportNotice,
				&boundingRect
			);

			{
				QFont font = painter.font();
				font.setPixelSize(8);
				painter.setFont(font);
			}

			painter.drawText(
				boundingRect.right() + boundingRect.height() / 5,
				boundingRect.top() + boundingRect.height() / 2,
				QChar(0x00AE)
			);
		}

		image.save(QString::fromStdWString(filePath.wstr()));
	}
}

void QtGraphicsView::copyNodeName()
{
	QApplication::clipboard()->setText(QString::fromStdWString(m_clipboardNodeName));
}

void QtGraphicsView::showDefinition()
{
	MessageCodeShowDefinition(m_hideNodeId).dispatch();
}

void QtGraphicsView::hideNode()
{
	MessageGraphNodeHide(m_hideNodeId).dispatch();
}

void QtGraphicsView::hideEdge()
{
	MessageGraphNodeHide(m_hideEdgeId).dispatch();
}

void QtGraphicsView::bookmarkNode()
{
	MessageDisplayBookmarkCreator(m_bookmarkNodeId).dispatch();
}

void QtGraphicsView::zoomInPressed()
{
	updateZoom(m_zoomInButtonSpeed);
}

void QtGraphicsView::zoomOutPressed()
{
	updateZoom(m_zoomOutButtonSpeed);
}

void QtGraphicsView::hideZoomLabel()
{
	m_zoomState->hide();
}

bool QtGraphicsView::moves() const
{
	return m_up || m_down || m_left || m_right;
}

void QtGraphicsView::setZoomFactor(float zoomFactor)
{
	m_zoomFactor = zoomFactor;

	m_zoomState->setText(QString::number(int(m_zoomFactor * 100)) + "%");
	updateTransform();

	m_zoomState->show();
	m_zoomLabelTimer->stop();
	m_zoomLabelTimer->start(1000);
}

void QtGraphicsView::updateTransform()
{
	float zoomFactor = m_appZoomFactor * m_zoomFactor;
	setTransform(QTransform(zoomFactor, 0, 0, zoomFactor, 0, 0));
}
