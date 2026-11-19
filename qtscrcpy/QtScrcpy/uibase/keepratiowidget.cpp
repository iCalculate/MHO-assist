#include <QResizeEvent>
#include <cmath>

#include "keepratiowidget.h"

KeepRatioWidget::KeepRatioWidget(QWidget *parent) : QWidget(parent) {}

KeepRatioWidget::~KeepRatioWidget() {}

void KeepRatioWidget::setWidget(QWidget *w)
{
    if (!w) {
        return;
    }
    w->setParent(this);
    m_subWidget = w;
}

void KeepRatioWidget::setWidthHeightRatio(float widthHeightRatio)
{
    if (fabs(m_widthHeightRatio - widthHeightRatio) < 0.000001f) {
        return;
    }
    m_widthHeightRatio = widthHeightRatio;
    adjustSubWidget();
}

const QSize KeepRatioWidget::goodSize()
{
    if (!m_subWidget || m_widthHeightRatio < 0.0f) {
        return QSize();
    }
    return m_subWidget->size();
}

void KeepRatioWidget::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event)
    adjustSubWidget();
}

void KeepRatioWidget::adjustSubWidget()
{
    if (!m_subWidget) {
        return;
    }

    QRect area = rect();
    const double ratio = m_widthHeightRatio > 0 ? m_widthHeightRatio : 1.0;
    if (m_diagnostics && m_showDiagnostics) {
        // A fixed horizontal header, never an overlay or a side panel.
        const int panelHeight = qBound(1, m_diagnostics->heightForWidth(area.width()), qMax(1, area.height() - 1));
        m_diagnostics->setGeometry(0, 0, area.width(), panelHeight);
        area.setTop(panelHeight);
        m_diagnostics->raise();
    }
    QSize fitted = m_widthHeightRatio > 0 ? QSize(qRound(ratio*10000),10000).scaled(area.size(),Qt::KeepAspectRatio) : area.size();
    m_subWidget->setGeometry(QRect(QPoint(area.x()+(area.width()-fitted.width())/2,
                                       area.y()+(area.height()-fitted.height())/2),fitted));
}

void KeepRatioWidget::setDiagnosticsWidget(QWidget *w)
{
    m_diagnostics=w;
    if (w) { w->setParent(this); w->setVisible(m_showDiagnostics); }
    adjustSubWidget();
}

void KeepRatioWidget::showDiagnostics(bool show)
{
    m_showDiagnostics=show;
    if(m_diagnostics) m_diagnostics->setVisible(show);
    adjustSubWidget();
}
