package dev.kiln;

import android.Manifest;
import android.app.job.JobInfo;
import android.app.job.JobScheduler;
import android.content.ComponentName;
import android.content.Context;
import android.content.pm.PackageManager;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.os.PersistableBundle;
import android.util.Base64;
import android.util.Log;

import java.util.ArrayList;
import java.util.List;

/**
 * kiln::work's JobScheduler (WorkManager's SystemJobScheduler and SystemJobInfoConverter): each
 * run is a job of {@link KilnJobService} carrying its WorkSpec, encoded by native code.
 */
final class KilnWork {
    private KilnWork() {}

    static final String EXTRA_SPEC = "dev.kiln.work.spec";
    private static Context context;

    static native void startJob(int jobId, byte[] spec);
    static native boolean stopJob(int jobId);

    static void attach(Context c) {
        if (context == null) context = c.getApplicationContext();
    }

    private static JobScheduler scheduler() {
        if (context == null) {
            Log.e("kiln", "work: no Context yet (KilnActivity or KilnJobService attaches it)");
            return null;
        }
        return context.getSystemService(JobScheduler.class);
    }

    // network: kiln::work::NetworkType's order.
    static boolean schedule(int jobId, byte[] spec, long delay, int network, boolean charging, boolean idle,
                            boolean batteryNotLow, boolean storageNotLow, boolean linear, long backoffDelay,
                            boolean expedited, boolean retry) {
        JobScheduler s = scheduler();
        if (s == null) return false;
        PersistableBundle extras = new PersistableBundle();
        extras.putString(EXTRA_SPEC, Base64.encodeToString(spec, Base64.NO_WRAP));
        JobInfo.Builder b = new JobInfo.Builder(jobId, new ComponentName(context, KilnJobService.class))
                .setRequiresCharging(charging)
                .setRequiresDeviceIdle(idle)
                .setRequiresBatteryNotLow(batteryNotLow)
                .setRequiresStorageNotLow(storageNotLow)
                .setExtras(extras);
        switch (network) {
            case 1: b.setRequiredNetworkType(JobInfo.NETWORK_TYPE_ANY); break;
            case 2: b.setRequiredNetworkType(JobInfo.NETWORK_TYPE_UNMETERED); break;
            case 3: b.setRequiredNetworkType(JobInfo.NETWORK_TYPE_NOT_ROAMING); break;
            case 4: b.setRequiredNetworkType(JobInfo.NETWORK_TYPE_METERED); break;
            case 5:
                b.setRequiredNetwork(new NetworkRequest.Builder()
                        .addCapability(NetworkCapabilities.NET_CAPABILITY_TEMPORARILY_NOT_METERED).build());
                break;
            default: b.setRequiredNetworkType(JobInfo.NETWORK_TYPE_NONE); break;
        }
        // Device idle and backoff criteria cannot be set together.
        if (!idle) b.setBackoffCriteria(backoffDelay, linear ? JobInfo.BACKOFF_POLICY_LINEAR : JobInfo.BACKOFF_POLICY_EXPONENTIAL);
        if (delay > 0) b.setMinimumLatency(delay);
        // Jobs outlive a reboot when the app may hear of it (WorkManager's
        // isJobSchedulerPersistenceEffective).
        b.setPersisted(context.checkSelfPermission(Manifest.permission.RECEIVE_BOOT_COMPLETED) == PackageManager.PERMISSION_GRANTED);
        // Retries and delayed runs cannot be expedited.
        if (expedited && !retry && delay <= 0) b.setExpedited(true);
        try {
            return s.schedule(b.build()) == JobScheduler.RESULT_SUCCESS;
        } catch (RuntimeException e) {
            Log.e("kiln", "work: cannot schedule job " + jobId, e);
            return false;
        }
    }

    static void cancel(int jobId) {
        JobScheduler s = scheduler();
        if (s != null) s.cancel(jobId);
    }

    /** The WorkSpecs of the scheduled jobs. */
    static byte[][] pending() {
        JobScheduler s = scheduler();
        if (s == null) return new byte[0][];
        List<byte[]> out = new ArrayList<>();
        ComponentName service = new ComponentName(context, KilnJobService.class);
        for (JobInfo j : s.getAllPendingJobs()) {
            String spec = j.getExtras().getString(EXTRA_SPEC);
            if (spec != null && service.equals(j.getService())) out.add(Base64.decode(spec, Base64.NO_WRAP));
        }
        return out.toArray(new byte[0][]);
    }

    static void finished(int jobId, boolean reschedule) {
        KilnJobService.finished(jobId, reschedule);
    }
}
