package dev.kiln.coordinator;

import android.app.Activity;
import android.os.Bundle;
import android.view.ViewGroup;

import androidx.coordinatorlayout.widget.CoordinatorLayout;

import com.google.android.material.appbar.AppBarLayout;
import com.google.android.material.appbar.MaterialToolbar;

import dev.kiln.KilnHost;
import dev.kiln.KilnView;

/**
 * A View app with kiln in it: a toolbar that scrolls away with the list below it. The list is
 * kiln's (src/main/cpp/main.cpp); its nested scroll reaches the CoordinatorLayout, which moves
 * the app bar first (AppBarLayout.ScrollingViewBehavior).
 */
public class MainActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        CoordinatorLayout root = new CoordinatorLayout(this);
        root.setFitsSystemWindows(true);

        AppBarLayout appBar = new AppBarLayout(this);
        MaterialToolbar toolbar = new MaterialToolbar(this);
        toolbar.setTitle("Toolbar from Views");
        AppBarLayout.LayoutParams toolbarParams = new AppBarLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        toolbarParams.setScrollFlags(AppBarLayout.LayoutParams.SCROLL_FLAG_SCROLL | AppBarLayout.LayoutParams.SCROLL_FLAG_ENTER_ALWAYS);
        appBar.addView(toolbar, toolbarParams);
        root.addView(appBar, new CoordinatorLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        KilnView kiln = KilnHost.create(this, "coordinator", savedInstanceState);
        CoordinatorLayout.LayoutParams kilnParams = new CoordinatorLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
        kilnParams.setBehavior(new AppBarLayout.ScrollingViewBehavior());
        root.addView(kiln, kilnParams);

        setContentView(root);
    }
}
