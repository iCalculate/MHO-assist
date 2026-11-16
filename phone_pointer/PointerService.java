package org.qtscrcpy.pointer;

import android.app.*;
import android.content.*;
import android.graphics.*;
import android.net.*;
import android.os.*;
import android.view.*;
import java.io.*;

/** Draw-only local companion: no network, storage, screen capture or input permissions. */
public final class PointerService extends Service {
    private final Handler ui=new Handler(Looper.getMainLooper());
    private WindowManager wm;
    private WindowManager.LayoutParams params;
    private Cursor view;
    private LocalServerSocket listener;
    private LocalSocket client;
    private volatile boolean running=true;
    private volatile long received=0;
    private volatile float nx=0,ny=0;
    private volatile boolean shown=false,pressed=false,connected=false;
    private volatile long clickSequence=0;
    private boolean added=false;
    private long lastApplied=-1;
    private final Runnable ticker=new Runnable(){public void run(){
        if(!running)return;
        long now=SystemClock.uptimeMillis();
        boolean visible=shown && connected && now-received<1500;
        if(visible && received!=lastApplied){
            Point size=new Point();wm.getDefaultDisplay().getRealSize(size);
            params.x=Math.round(nx*(size.x-1))-params.width/2;params.y=Math.round(ny*(size.y-1))-params.height/2;
            view.interaction(pressed,clickSequence);
            try{if(!added){wm.addView(view,params);added=true;}else wm.updateViewLayout(view,params);lastApplied=received;}
            catch(RuntimeException error){android.util.Log.e("QtScrcpyPointer","Overlay unavailable",error);stopSelf();return;}
        }else if(!visible && added){wm.removeView(view);added=false;lastApplied=-1;}
        if(!connected && now-received>30000){stopSelf();return;}
        ui.postDelayed(this,33);
    }};
    @Override public void onCreate(){
        super.onCreate();
        NotificationManager manager=(NotificationManager)getSystemService(NOTIFICATION_SERVICE);
        manager.createNotificationChannel(new NotificationChannel("pointer","手机鼠标提示",NotificationManager.IMPORTANCE_MIN));
        Notification notification=new Notification.Builder(this,"pointer").setSmallIcon(android.R.drawable.ic_menu_mylocation)
            .setContentTitle("QtScrcpy 手机光标").setContentText("普通鼠标模式的位置提示；断开后自动隐藏").build();
        startForeground(1,notification);
        wm=(WindowManager)getSystemService(WINDOW_SERVICE);view=new Cursor(this);
        float density=getResources().getDisplayMetrics().density;
        params=new WindowManager.LayoutParams(Math.round(64*density),Math.round(64*density),WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
            WindowManager.LayoutParams.FLAG_NOT_FOCUSABLE|WindowManager.LayoutParams.FLAG_NOT_TOUCHABLE|WindowManager.LayoutParams.FLAG_LAYOUT_IN_SCREEN|WindowManager.LayoutParams.FLAG_LAYOUT_NO_LIMITS,PixelFormat.TRANSLUCENT);
        params.gravity=Gravity.TOP|Gravity.LEFT;params.alpha=.7f;params.setTitle("QtScrcpyPointer");
        if(Build.VERSION.SDK_INT>=28)params.layoutInDisplayCutoutMode=WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        received=SystemClock.uptimeMillis();ui.post(ticker);
        new Thread(new Runnable(){public void run(){serve();}},"local-pointer").start();
    }
    private void serve(){
        try{
            listener=new LocalServerSocket("qtscrcpy-pointer");
            while(running){
                client=listener.accept();connected=true;received=SystemClock.uptimeMillis();
                client.getOutputStream().write("READY 2\n".getBytes("UTF-8"));
                try{
                    BufferedReader input=new BufferedReader(new InputStreamReader(client.getInputStream(),"UTF-8"));
                    String line;
                    while(running && (line=input.readLine())!=null){
                        if(line.length()>100)break;
                        String[] fields=line.trim().split(" ");
                        if(fields.length!=3 && fields.length!=4)continue;
                        float x=Float.parseFloat(fields[0]),y=Float.parseFloat(fields[1]);
                        if(Float.isNaN(x)||Float.isNaN(y))continue;
                        nx=Math.max(0,Math.min(1,x));ny=Math.max(0,Math.min(1,y));
                        shown=!"0".equals(fields[2]);pressed="2".equals(fields[2]);
                        if(fields.length==4)clickSequence=Long.parseLong(fields[3]);
                        received=SystemClock.uptimeMillis();
                    }
                }catch(IOException|NumberFormatException ignored){}
                finally{connected=false;shown=false;received=SystemClock.uptimeMillis();client.close();client=null;}
            }
        }catch(IOException error){if(running)android.util.Log.e("QtScrcpyPointer","Local socket stopped",error);}
    }
    @Override public int onStartCommand(Intent intent,int flags,int id){return START_NOT_STICKY;}
    @Override public IBinder onBind(Intent intent){return null;}
    @Override public void onDestroy(){
        running=false;ui.removeCallbacks(ticker);if(added){wm.removeView(view);added=false;}
        try{if(client!=null)client.close();if(listener!=null)listener.close();}catch(IOException ignored){}
        stopForeground(true);super.onDestroy();
    }
    private static final class Cursor extends View{
        private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
        private boolean down=false;
        private long seen=0,released=-1000,lastDraw=0;
        private float radius=7;
        Cursor(Context context){super(context);}
        void interaction(boolean pressed,long sequence){
            if((down && !pressed) || (!pressed && sequence!=seen))released=SystemClock.uptimeMillis();
            down=pressed;seen=sequence;invalidate();
        }
        @Override protected void onDraw(Canvas canvas){
            long now=SystemClock.uptimeMillis();float dt=Math.min(50,now-lastDraw);lastDraw=now;
            float target=down?10:7;radius+=(target-radius)*Math.min(1,dt/65f);
            float scale=getResources().getDisplayMetrics().density;canvas.save();canvas.translate(getWidth()/2f,getHeight()/2f);canvas.scale(scale,scale);
            paint.setStyle(Paint.Style.FILL);paint.setColor(Color.argb(32,0,0,0));canvas.drawCircle(0,0,radius+1.6f,paint);
            paint.setColor(Color.argb(down?155:78,255,255,255));canvas.drawCircle(0,0,radius,paint);
            paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(down?1.3f:1);
            paint.setColor(Color.argb(down?220:140,255,255,255));canvas.drawCircle(0,0,radius,paint);
            float progress=(now-released)/320f;
            if(progress>=0 && progress<1){paint.setStrokeWidth(1.2f);paint.setColor(Color.argb((int)(145*(1-progress)),255,255,255));canvas.drawCircle(0,0,10+14*progress,paint);}
            canvas.restore();if(Math.abs(radius-target)>.05 || (progress>=0 && progress<1))postInvalidateOnAnimation();
        }
    }
}
