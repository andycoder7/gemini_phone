
package com.example.hellojni;

import android.app.Activity;
import android.widget.TextView;
import android.os.Bundle;


public class HelloJni extends Activity{
    @Override
    public void onCreate(Bundle savedInstanceState){
        super.onCreate(savedInstanceState);
        TextView  tv = new TextView(this);
        tv.setText( stringFromJNI() + "" );
        setContentView(tv);
    }

    public static native int stringFromJNI();

    static {
        System.loadLibrary("hello-jni");
    }
}
