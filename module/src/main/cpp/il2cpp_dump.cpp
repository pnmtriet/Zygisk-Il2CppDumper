//
// Modified by your sleepless friend.
//

#include "il2cpp_dump.h"
#include <dlfcn.h>
#include <cstdlib>
#include <cstring>
#include <cinttypes>
#include <string>
#include <vector>
#include <sstream>
#include <fstream>
#include <unistd.h>
#include "xdl.h"
#include "log.h"
#include "il2cpp-tabledefs.h"
#include "il2cpp-class.h"

#define DO_API(r, n, p) r (*n) p
#include "il2cpp-api-functions.h"
#undef DO_API

static uint64_t il2cpp_base = 0;

void init_il2cpp_api(void *handle) {
#define DO_API(r, n, p) {                      \
    n = (r (*) p)xdl_sym(handle, #n, nullptr); \
    if(!n) { LOGW("api not found %s", #n); }   \
}
#include "il2cpp-api-functions.h"
#undef DO_API
}

std::string get_method_modifier(uint32_t flags) {
    std::stringstream out;
    auto access = flags & METHOD_ATTRIBUTE_MEMBER_ACCESS_MASK;
    switch (access) {
        case METHOD_ATTRIBUTE_PRIVATE: out << "private "; break;
        case METHOD_ATTRIBUTE_PUBLIC: out << "public "; break;
        case METHOD_ATTRIBUTE_FAMILY: out << "protected "; break;
        case METHOD_ATTRIBUTE_ASSEM:
        case METHOD_ATTRIBUTE_FAM_AND_ASSEM: out << "internal "; break;
        case METHOD_ATTRIBUTE_FAM_OR_ASSEM: out << "protected internal "; break;
    }
    if (flags & METHOD_ATTRIBUTE_STATIC) out << "static ";
    if (flags & METHOD_ATTRIBUTE_ABSTRACT) {
        out << "abstract ";
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_REUSE_SLOT)
            out << "override ";
    } else if (flags & METHOD_ATTRIBUTE_FINAL) {
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_REUSE_SLOT)
            out << "sealed override ";
    } else if (flags & METHOD_ATTRIBUTE_VIRTUAL) {
        if ((flags & METHOD_ATTRIBUTE_VTABLE_LAYOUT_MASK) == METHOD_ATTRIBUTE_NEW_SLOT)
            out << "virtual ";
        else out << "override ";
    }
    if (flags & METHOD_ATTRIBUTE_PINVOKE_IMPL) out << "extern ";
    return out.str();
}

bool _il2cpp_type_is_byref(const Il2CppType *type) {
    auto byref = type->byref;
    if (il2cpp_type_is_byref) byref = il2cpp_type_is_byref(type);
    return byref;
}

// --- giữ nguyên các hàm dump_method, dump_property, dump_field, dump_type như cũ ---
// chỉ thay phần init + dump ở dưới.

void il2cpp_api_init(void *handle) {
    LOGI("il2cpp_handle: %p", handle);
    init_il2cpp_api(handle);

    if (il2cpp_domain_get_assemblies) {
        Dl_info dlInfo;
        if (dladdr((void *) il2cpp_domain_get_assemblies, &dlInfo)) {
            il2cpp_base = reinterpret_cast<uint64_t>(dlInfo.dli_fbase);
        }
        LOGI("il2cpp_base: %" PRIx64"", il2cpp_base);
    } else {
        LOGE("Failed to initialize il2cpp api.");
        return;
    }

    // --- thêm delay thủ công ---
    LOGI("Sleeping 10s before checking il2cpp init...");
    sleep(10);

    // --- chờ init ---
    int retry = 0;
    while (!il2cpp_is_vm_thread(nullptr)) {
        retry++;
        LOGI("Waiting for il2cpp_init... try %d", retry);
        sleep(1);
        if (retry > 30) {
            LOGE("Timeout waiting for il2cpp_init after %d seconds.", retry + 10);
            return;
        }
    }

    auto domain = il2cpp_domain_get();
    if (!domain) {
        LOGE("il2cpp_domain_get() failed after init wait.");
        return;
    }

    il2cpp_thread_attach(domain);
    LOGI("il2cpp thread attached successfully after %d seconds total.", retry + 10);
}

void il2cpp_dump(const char *ignored) {
    LOGI("Starting dump...");
    size_t size = 0;
    auto domain = il2cpp_domain_get();
    if (!domain) {
        LOGE("il2cpp_domain_get() returned null");
        return;
    }

    auto assemblies = il2cpp_domain_get_assemblies(domain, &size);
    if (!assemblies || size == 0) {
        LOGE("No assemblies found!");
        return;
    }

    std::stringstream imageOutput;
    for (int i = 0; i < size; ++i) {
        auto image = il2cpp_assembly_get_image(assemblies[i]);
        imageOutput << "// Image " << i << ": " << il2cpp_image_get_name(image) << "\n";
    }

    std::vector<std::string> outputs;

    if (il2cpp_image_get_class) {
        LOGI("Detected version >= 2018.3");
        for (int i = 0; i < size; ++i) {
            auto image = il2cpp_assembly_get_image(assemblies[i]);
            std::stringstream header;
            header << "\n// Dll : " << il2cpp_image_get_name(image);
            auto classCount = il2cpp_image_get_class_count(image);
            for (int j = 0; j < classCount; ++j) {
                auto klass = il2cpp_image_get_class(image, j);
                auto type = il2cpp_class_get_type(const_cast<Il2CppClass *>(klass));
                outputs.push_back(header.str() + dump_type(type));
            }
        }
    } else {
        LOGI("Detected version < 2018.3 (reflection mode)");
    }

    // --- ghi file ra /sdcard/Download ---
    const char *dumpPath = "/sdcard/Download/dump.cs";
    LOGI("Writing dump to: %s", dumpPath);

    std::ofstream out(dumpPath);
    if (!out.is_open()) {
        LOGE("Cannot open output file!");
        return;
    }

    out << imageOutput.str();
    for (auto &s : outputs) out << s;
    out.close();

    LOGI("Dump complete! Total %zu classes written.", outputs.size());
}
