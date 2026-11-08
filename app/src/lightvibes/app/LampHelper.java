package lightvibes.app;

import android.os.IBinder;
import android.os.Parcel;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.lang.reflect.Method;

/**
 * Persistent Binder helper started by lightvibesd via /system/bin/app_process.
 * Caches the com.hkmc.micom.system IBinder handle so RGB and brightness transactions
 * execute in under 1 ms without forking /system/bin/service on every light change.
 */
public final class LampHelper {
    private static final String DESCRIPTOR = "com.hkmc.micom.ISystemService";
    private static Method getServiceMethod;
    private static String serviceName = "com.hkmc.micom.system";
    private static IBinder binder;

    private static IBinder getBinder() throws Exception {
        if (binder != null && binder.isBinderAlive()) return binder;
        if (getServiceMethod == null) {
            Class<?> sm = Class.forName("android.os.ServiceManager");
            getServiceMethod = sm.getMethod("getService", String.class);
        }
        binder = (IBinder) getServiceMethod.invoke(null, serviceName);
        return binder;
    }

    private static void transactRgb(int r, int g, int b) throws Exception {
        IBinder svc = getBinder();
        if (svc == null) return;
        Parcel data = Parcel.obtain(), reply = Parcel.obtain();
        try {
            data.writeInterfaceToken(DESCRIPTOR);
            data.writeByteArray(new byte[]{(byte) ((r >> 8) & 0xff), (byte) (r & 0xff)});
            data.writeByteArray(new byte[]{(byte) ((g >> 8) & 0xff), (byte) (g & 0xff)});
            data.writeByteArray(new byte[]{(byte) ((b >> 8) & 0xff), (byte) (b & 0xff)});
            if (!svc.transact(78, data, reply, 0)) binder = null;
            else reply.readException();
        } finally {
            reply.recycle();
            data.recycle();
        }
    }

    private static void transactConfig(int mode, int val) throws Exception {
        IBinder svc = getBinder();
        if (svc == null) return;
        Parcel data = Parcel.obtain(), reply = Parcel.obtain();
        try {
            data.writeInterfaceToken(DESCRIPTOR);
            data.writeInt(mode);
            data.writeInt(val);
            if (!svc.transact(77, data, reply, 0)) binder = null;
            else reply.readException();
        } finally {
            reply.recycle();
            data.recycle();
        }
    }

    private static void transactRequestStatus() throws Exception {
        IBinder svc = getBinder();
        if (svc == null) return;
        Parcel data = Parcel.obtain(), reply = Parcel.obtain();
        try {
            data.writeInterfaceToken(DESCRIPTOR);
            if (!svc.transact(76, data, reply, 0)) binder = null;
            else reply.readException();
        } finally {
            reply.recycle();
            data.recycle();
        }
    }

    public static void main(String[] args) {
        if (args.length > 0 && args[0] != null && !args[0].isEmpty()) {
            serviceName = args[0];
        }
        try {
            getBinder();
        } catch (Throwable ignored) {
        }
        System.out.println("ready");
        System.out.flush();

        BufferedReader in = new BufferedReader(new InputStreamReader(System.in));
        String line;
        try {
            while ((line = in.readLine()) != null) {
                String[] p = line.trim().split("\\s+");
                if (p.length == 0 || p[0].isEmpty()) continue;
                boolean sync = "sync".equals(p[p.length - 1]);
                try {
                    if ("rgb".equals(p[0]) && p.length >= 4) {
                        transactRgb(Integer.parseInt(p[1]), Integer.parseInt(p[2]), Integer.parseInt(p[3]));
                    } else if ("cfg".equals(p[0]) && p.length >= 3) {
                        transactConfig(Integer.parseInt(p[1]), Integer.parseInt(p[2]));
                    } else if ("req".equals(p[0])) {
                        transactRequestStatus();
                    }
                } catch (Throwable t) {
                    binder = null;
                }
                if (sync) {
                    System.out.println("ok");
                    System.out.flush();
                }
            }
        } catch (Throwable ignored) {
        }
    }
}
