#ifndef KEEPRATIOWIDGET_H
#define KEEPRATIOWIDGET_H

#include <QPointer>
#include <QWidget>

class KeepRatioWidget : public QWidget
{
    Q_OBJECT
public:
    explicit KeepRatioWidget(QWidget *parent = nullptr);
    ~KeepRatioWidget();

    void setWidget(QWidget *w);
    void setDiagnosticsWidget(QWidget *w);
    void showDiagnostics(bool show);
    void setWidthHeightRatio(float widthHeightRatio);
    const QSize goodSize();

protected:
    void resizeEvent(QResizeEvent *event);
    void adjustSubWidget();

private:
    float m_widthHeightRatio = -1.0f;
    QPointer<QWidget> m_subWidget;
    QSize m_goodSize;
    QPointer<QWidget> m_diagnostics;
    bool m_showDiagnostics = false;
};

#endif // KEEPRATIOWIDGET_H
