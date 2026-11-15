package dev.kiln;

import android.Manifest;
import android.app.job.JobInfo;
import android.app.job.JobScheduler;
import android.content.ComponentName;
import android.content.Context;
import android.content.SharedPreferences;
import android.content.pm.PackageManager;
import android.net.NetworkCapabilities;
import android.net.NetworkRequest;
import android.os.PersistableBundle;
import android.util.Base64;
import android.util.Log;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/**
 * kiln::work's JobScheduler (WorkManager's SystemJobScheduler and SystemJobInfoConverter): each
 * run is a job of {@link KilnJobService} carrying its WorkSpec, encoded by native code.
 */
final class KilnWork {
    private KilnWork() {}

    static final String EXTRA_SPEC = "dev.kiln.work.spec";
    // Each scheduled job's arguments, kept until it is done: a force stop cancels the app's
    // jobs, and a job not persisted does not outlive a reboot (WorkManager keeps its database
    // and reschedules from it, ForceStopRunnable).
    private static final String PREFS = "kiln_work";
    private static Context context;

    static native void startJob(int jobId, byte[] spec);
    static native boolean stopJob(int jobId);

    static void attach(Context c) {
        if (context != null) return;
        context = c.getApplicationContext();
        restoreLost();
    }

    private static SharedPreferences prefs() { return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE); }

    /** Kept jobs the scheduler no longer has, scheduled again (at once when they are due). */
    private static void restoreLost() {
        JobScheduler s = context.getSystemService(JobScheduler.class);
        if (s == null) return;
        for (Map.Entry<String, ?> e : prefs().getAll().entrySet()) {
            try {
                int jobId = Integer.parseInt(e.getKey());
                if (s.getPendingJob(jobId) != null) continue;
                String[] f = ((String) e.getValue()).split("\\|");
                if (f.length != 11) continue;
                long delay = Math.max(0, Long.parseLong(f[1]) - System.currentTimeMillis());
                schedule(jobId, Base64.decode(f[0], Base64.NO_WRAP), delay, Integer.parseInt(f[2]), f[3].equals("1"), f[4].equals("1"),
                         f[5].equals("1"), f[6].equals("1"), f[7].equals("1"), Long.parseLong(f[8]), f[9].equals("1"), f[10].equals("1"));
            } catch (RuntimeException ex) {
                Log.w("kiln", "work: cannot restore job " + e.getKey(), ex);
            }
        }
    }

    private static String bit(boolean b) { return b ? "1" : "0"; }

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
        prefs().edit().putString(Integer.toString(jobId), Base64.encodeToString(spec, Base64.NO_WRAP) + "|" + (System.currentTimeMillis() + Math.max(0, delay))
                + "|" + network + "|" + bit(charging) + "|" + bit(idle) + "|" + bit(batteryNotLow) + "|" + bit(storageNotLow) + "|" + bit(linear)
                + "|" + backoffDelay + "|" + bit(expedited) + "|" + bit(retry)).apply();
        try {
            return s.schedule(b.build()) == JobScheduler.RESULT_SUCCESS;
        } catch (RuntimeException e) {
            Log.e("kiln", "work: cannot schedule job " + jobId, e);
            return false;
        }
    }

    static void cancel(int jobId) {
        JobScheduler s = scheduler();
        if (s == null) return;
        prefs().edit().remove(Integer.toString(jobId)).apply();
        s.cancel(jobId);
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
        // Done, unless the system runs it again; a next run (retry, period) is scheduled after.
        if (!reschedule && context != null) prefs().edit().remove(Integer.toString(jobId)).apply();
        KilnJobService.finished(jobId, reschedule);
    }
}
