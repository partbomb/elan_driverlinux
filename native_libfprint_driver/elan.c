#define FP_COMPONENT "device_elan"

#include "drivers_api.h"
#include "sift_engine.h"

struct _FpiDeviceElan {
    FpDevice parent;
};

G_DECLARE_FINAL_TYPE (FpiDeviceElan, fpi_device_elan, FPI, DEVICE_ELAN, FpDevice);
G_DEFINE_TYPE (FpiDeviceElan, fpi_device_elan, FP_TYPE_DEVICE);

static void elan_open(FpDevice *device) {
    sift_engine_init();
    fpi_device_open_complete(device, NULL);
}

static void elan_close(FpDevice *device) {
    fpi_device_close_complete(device, NULL);
}

// Отложенная функция для Enroll
static gboolean elan_enroll_timeout(gpointer user_data) {
    FpDevice *device = FP_DEVICE(user_data);
    FpPrint *print = NULL;
    fpi_device_get_enroll_data(device, &print);
    
    unsigned char *data = NULL;
    int size = sift_engine_enroll(&data);
    
    if (size > 0 && print != NULL) {
        GVariant *record = g_variant_new_fixed_array(G_VARIANT_TYPE_BYTE, data, size, sizeof(guchar));
        g_object_set(print, "fpi-type", FPI_PRINT_RAW, "fpi-data", record, NULL);
        fpi_device_enroll_complete(device, g_object_ref(print), NULL);
        free(data);
    } else {
        fpi_device_enroll_complete(device, NULL, fpi_device_error_new(FP_DEVICE_ERROR_GENERAL));
    }
    return G_SOURCE_REMOVE;
}

static void elan_enroll(FpDevice *device) {
    // Ждем 100мс перед тем, как ответить
    g_timeout_add(100, elan_enroll_timeout, device);
}

// Отложенная функция для Verify
static gboolean elan_verify_timeout(gpointer user_data) {
    FpDevice *device = FP_DEVICE(user_data);
    FpPrint *print = NULL;
    fpi_device_get_verify_data(device, &print);
    
    int match = 0;
    if (print) {
        GVariant *record = NULL;
        g_object_get(print, "fpi-data", &record, NULL);
        if (record) {
            gsize size;
            const unsigned char *data = (const unsigned char *)g_variant_get_fixed_array(record, &size, sizeof(guchar));
            match = sift_engine_verify(data, (int)size);
            g_variant_unref(record);
        }
    }

    // Если совпало - отдаем отпечаток, если нет - отдаем NULL
    fpi_device_verify_report(device, match ? FPI_MATCH_SUCCESS : FPI_MATCH_FAIL, match ? print : NULL, NULL);
    fpi_device_verify_complete(device, NULL);
    
    return G_SOURCE_REMOVE;
}

static void elan_verify(FpDevice *device) {
    // Ждем 100мс, даем fprintd проснуться
    g_timeout_add(100, elan_verify_timeout, device);
}

static const FpIdEntry elan_id_table[] = {
    { .vid = 0x04f3, .pid = 0x0c4f },
    { .vid = 0, .pid = 0 }
};

static void fpi_device_elan_class_init(FpiDeviceElanClass *klass) {
    FpDeviceClass *dev_class = FP_DEVICE_CLASS(klass);

    dev_class->id = "elan";
    dev_class->full_name = "Elan Trojan SIFT";
    dev_class->type = FP_DEVICE_TYPE_USB;
    dev_class->id_table = elan_id_table;
    dev_class->scan_type = FP_SCAN_TYPE_PRESS;
    dev_class->features = FP_DEVICE_FEATURE_VERIFY; 
    
    dev_class->open = elan_open;
    dev_class->close = elan_close;
    dev_class->enroll = elan_enroll;
    dev_class->verify = elan_verify;
}

static void fpi_device_elan_init(FpiDeviceElan *self) {
}