package dev.kiln;

import android.app.job.JobParameters;
import android.app.job.JobService;
import android.util.Base64;
import android.util.Log;

import java.util.HashMap;
import java.util.Map;

/**
 * Runs kiln::work's jobs (WorkManager's SystemJobService): loads the app's library, which may be
 * the first thing this process does, and hands the job's WorkSpec to native code. The manifest
 * declares it with android.permission.BIND_JOB_SERVICE.
 */
public class KilnJobService extends JobService {
    private static KilnJobService current;
    private final Map<Integer, JobParameters> running = new HashMap<>();
    private int starting = -1;   // the job onStartJob is handing over (finishing at once: no jobFinished)

    @Override
    public void onCreate() {
        super.onCreate();
        current = this;
        KilnProcess.ensureLoaded(this);
    }

    @Override
    public void onDestroy() {
        if (current == this) current = null;
        super.onDestroy();
    }


    @Override
    public boolean onStartJob(JobParameters params) {
        String spec = params.getExtras().getString(KilnWork.EXTRA_SPEC);
        if (spec == null) return false;
        running.put(params.getJobId(), params);
        starting = params.getJobId();
        try {
            KilnWork.startJob(params.getJobId(), Base64.decode(spec, Base64.NO_WRAP));
        } catch (RuntimeException e) {
            Log.e("kiln", "work: job " + params.getJobId() + " did not start", e);
            running.remove(params.getJobId());
            return false;
        } finally {
            starting = -1;
        }
        return running.containsKey(params.getJobId());   // false: it finished already
    }

    @Override
    public boolean onStopJob(JobParameters params) {
        running.remove(params.getJobId());
        return KilnWork.stopJob(params.getJobId());
    }

    static void finished(int jobId, boolean reschedule) {
        KilnJobService s = current;
        if (s == null) return;
        JobParameters p = s.running.remove(jobId);
        if (p != null && s.starting != jobId) s.jobFinished(p, reschedule);
    }
}
