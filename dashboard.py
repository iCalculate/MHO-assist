"""Lightweight Qt-painted charts. No plotting process in the capture loop."""
import math
from PySide6 import QtCore,QtGui,QtWidgets
from language import tr

COLORS={'up':'#e8bf68','down':'#c39af3','left':'#69d7ad','right':'#79b4fa'}
SYMBOLS={'up':'↑','down':'↓','left':'←','right':'→'}

class Chart(QtWidgets.QWidget):
    def __init__(self,kind):
        super().__init__();self.kind=kind;self.data={};self.lang='zh';self.hits=[]
        self.setMinimumSize(240,112);self.setMouseTracking(True)
    def set_data(self,data,lang):self.data=data;self.lang=lang;self.update()
    def mouseMoveEvent(self,event):
        self.setToolTip(next((text for rect,text in self.hits if rect.contains(event.position().toPoint())),''))
    def paintEvent(self,event):
        p=QtGui.QPainter(self);p.setRenderHint(QtGui.QPainter.Antialiasing);self.hits=[]
        p.setBrush(QtGui.QColor('#141f2d'));p.setPen(QtGui.QColor('#304055'));p.drawRoundedRect(self.rect().adjusted(0,0,-1,-1),6,6)
        p.setFont(QtGui.QFont('Microsoft YaHei',9));p.setPen(QtGui.QColor('#a8bad1'));p.drawText(10,20,tr(self.kind,self.lang))
        r=QtCore.QRectF(12,31,self.width()-24,self.height()-42)
        d=self.data
        if not d.get('rounds') and self.kind!='latency' or self.kind=='latency' and not d.get('timing'):
            p.setPen(QtGui.QColor('#70849c'));p.drawText(r,QtCore.Qt.AlignCenter,tr('empty',self.lang));return
        if self.kind=='directions':
            counts=d['directions'];maximum=max(counts.values()) or 1
            for i,k in enumerate(COLORS):
                y=r.top()+i*r.height()/4;h=max(4,r.height()/4-5)
                p.setPen(QtGui.QColor(COLORS[k]));p.drawText(QtCore.QRectF(r.left(),y,20,h+4),QtCore.Qt.AlignCenter,SYMBOLS[k])
                x=r.left()+27;w=max(1,(r.width()-104)*counts[k]/maximum)
                p.fillRect(QtCore.QRectF(x,y+2,r.width()-104,h),QtGui.QColor('#253247'))
                p.fillRect(QtCore.QRectF(x,y+2,w,h),QtGui.QColor(COLORS[k]))
                p.setPen(QtGui.QColor('#d8e4f2'));p.drawText(QtCore.QRectF(r.right()-70,y,70,h+4),QtCore.Qt.AlignRight|QtCore.Qt.AlignVCenter,f"{counts[k]}  {counts[k]/max(1,d['keys']):.0%}")
        elif self.kind=='recent':
            rows=d['recent_rounds'];maximum=max(len(x['sequence']) for x in rows);pitch=r.width()/max(1,len(rows));base=r.bottom()-15
            for i,item in enumerate(rows):
                seq=item['sequence'];x=r.left()+i*pitch+pitch*.23;w=pitch*.54;y=base
                for k in COLORS:
                    height=(r.height()-30)*seq.count(k)/max(1,maximum)
                    p.fillRect(QtCore.QRectF(x,y-height,w,height),QtGui.QColor(COLORS[k]));y-=height
                p.setPen(QtGui.QColor('#d8e4f2'));p.drawText(QtCore.QRectF(x-3,y-17,w+6,15),QtCore.Qt.AlignCenter,str(len(seq)))
                p.setPen(QtGui.QColor('#8194ad'));p.drawText(QtCore.QRectF(x-7,base+2,w+14,15),QtCore.Qt.AlignCenter,str(d['rounds']-len(rows)+i+1))
                counts='  '.join(f'{SYMBOLS[k]} {seq.count(k)}' for k in COLORS)
                detail=f"#{d['rounds']-len(rows)+i+1}  "+' '.join(SYMBOLS[k] for k in seq)+f"\n{counts}\n{tr('confidence',self.lang)} {item['confidence']:.1%}"
                self.hits.append((QtCore.QRect(int(r.left()+i*pitch),int(r.top()),int(pitch),int(r.height())),detail))
        elif self.kind=='conf_hist':
            counts=d['confidence_hist'];maximum=max(counts) or 1;pitch=r.width()/7;base=r.bottom()-15
            for i,(label,n) in enumerate(zip(['<80','80–85','85–88','88–90','90–92','92–95','≥95'],counts)):
                x=r.left()+i*pitch+pitch*.23;w=pitch*.54;height=(r.height()-30)*n/maximum
                p.fillRect(QtCore.QRectF(x,base-height,w,height),QtGui.QColor(['#7587aa','#899bc8','#9d9bd7','#a49fe3','#83badf','#73d6d0','#80e6b2'][i]))
                p.setPen(QtGui.QColor('#d8e4f2'));p.drawText(QtCore.QRectF(x-4,base-height-18,w+8,15),QtCore.Qt.AlignCenter,str(n))
                p.setPen(QtGui.QColor('#8194ad'));p.drawText(QtCore.QRectF(r.left()+i*pitch,base+2,pitch,15),QtCore.Qt.AlignCenter,label)
        else:
            points=d['timing'];maximum=max(1.,math.ceil(max(v['mean'] for v in points)*1.2));plot=r.adjusted(24,5,-3,-12)
            p.setPen(QtGui.QColor('#29394d'))
            for i in range(3):
                y=plot.bottom()-i*plot.height()/2;p.drawLine(QtCore.QPointF(plot.left(),y),QtCore.QPointF(plot.right(),y))
                p.setPen(QtGui.QColor('#8194ad'));p.drawText(QtCore.QRectF(r.left()-3,y-8,23,15),QtCore.Qt.AlignRight,f'{maximum*i/2:g}');p.setPen(QtGui.QColor('#29394d'))
            path=QtGui.QPainterPath()
            for i,item in enumerate(points):
                x=plot.left()+plot.width()*i/max(1,len(points)-1);y=plot.bottom()-plot.height()*item['mean']/maximum
                if not i:path.moveTo(x,y)
                else:path.lineTo(x,y)
                tip=f"{item['time']}\n{item['mean']:.2f} ms · n={item['n']}\nmin {item['low']:.2f} / max {item['high']:.2f} ms"
                self.hits.append((QtCore.QRect(int(x-6),int(plot.top()),12,int(plot.height())),tip))
            p.setPen(QtGui.QPen(QtGui.QColor('#71cbe0'),1.8));p.drawPath(path)
            p.setPen(QtGui.QColor('#a8bad1'));p.drawText(QtCore.QRectF(plot.left(),plot.bottom()+3,plot.width(),15),QtCore.Qt.AlignRight,f"{points[-1]['mean']:.2f} ms")

class Dashboard(QtWidgets.QWidget):
    def __init__(self):
        super().__init__();self.lang='zh';self.snapshot={}
        layout=QtWidgets.QVBoxLayout(self);layout.setContentsMargins(0,0,0,0);layout.setSpacing(6)
        cards=QtWidgets.QHBoxLayout();cards.setSpacing(6);self.cards={}
        for key in ['rounds','keys','confidence','speed']:
            frame=QtWidgets.QFrame();frame.setStyleSheet('QFrame{background:#202d3d;border-radius:6px;}');box=QtWidgets.QVBoxLayout(frame);box.setContentsMargins(10,5,10,6);box.setSpacing(0)
            label=QtWidgets.QLabel();label.setStyleSheet('font-size:11px;color:#91a6c0');value=QtWidgets.QLabel('—');value.setStyleSheet('font-size:20px;color:#e7f0fb;font-weight:600')
            box.addWidget(label);box.addWidget(value);cards.addWidget(frame);self.cards[key]=(label,value)
        layout.addLayout(cards)
        grid=QtWidgets.QGridLayout();grid.setSpacing(6);self.charts=[]
        for i,kind in enumerate(['directions','recent','conf_hist','latency']):
            chart=Chart(kind);grid.addWidget(chart,i//2,i%2);self.charts.append(chart)
        layout.addLayout(grid,1)
        self.outcomes=QtWidgets.QLabel();self.outcomes.setStyleSheet('font-size:11px;color:#99adc6');layout.addWidget(self.outcomes)
    def set_data(self,data,lang):
        self.snapshot=data;self.lang=lang
        for key,(label,value) in self.cards.items():
            label.setText(tr(key,lang))
            value.setText(f"{data['confidence']:.1%}" if key=='confidence' and data['rounds'] else f"{data['mean_ms']:.2f} ms" if key=='speed' and data['frames'] else str(data[key]) if key in ('rounds','keys') else '—')
        for chart in self.charts:chart.set_data(data,lang)
        self.outcomes.setText((f"已发送 {data['sent_rounds']} 轮 / {data['sent_keys']} 键    ·    已确认 {data['confirmed_rounds']} 轮 / {data['confirmed_keys']} 键    ·    未确认 {data['unconfirmed_rounds']} 轮" if lang=='zh' else f"Sent {data['sent_rounds']}r / {data['sent_keys']}k    ·    Confirmed {data['confirmed_rounds']}r / {data['confirmed_keys']}k    ·    Unverified {data['unconfirmed_rounds']}r"))
        self.outcomes.setToolTip(tr('scope_hint',lang));self.cards['speed'][0].setToolTip(tr('speed_tip',lang))
        self.cards['confidence'][0].setToolTip(tr('confidence_tip',lang))
