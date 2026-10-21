public class SetLocale {
    public static void main(String[] args) throws Exception {
        Class<?> amn = Class.forName("android.app.ActivityManagerNative");
        Object am = amn.getMethod("getDefault").invoke(null);
        Class<?> cfgClass = Class.forName("android.content.res.Configuration");
        Object config = am.getClass().getMethod("getConfiguration").invoke(am);
        java.util.Locale locale = new java.util.Locale(args[0], args[1]);
        cfgClass.getField("locale").set(config, locale);
        // userSetLocale makes system_server persist the choice to persist.sys.language/country.
        cfgClass.getField("userSetLocale").setBoolean(config, true);
        am.getClass().getMethod("updateConfiguration", cfgClass).invoke(am, config);
        System.out.println("locale set to " + locale);
    }
}
