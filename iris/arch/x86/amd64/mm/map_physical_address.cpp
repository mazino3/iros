#include <iris/mm/map_physical_address.h>
#include <limine.h>

extern "C" {
// HHDM refers to "higher-half direct map", which provides
// a complete mapping of all physical memory at a fixed virtual
// offset.
static volatile limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0,
    .response = nullptr,
};
}

namespace iris::mm {
Expected<PhysicalAddressMapping> map_physical_address(PhysicalAddress address, usize byte_size) {
    // FIXME: this only works with the bootloader's page tables for now.
    // FIXME: validate that the physical address mapping is reasonable (only up to 4 GiB if only 4 GiB of memory are available).
    auto virtual_address = reinterpret_cast<di::Byte*>(hhdm_request.response->offset + address.raw_address());
    return PhysicalAddressMapping({ virtual_address, byte_size });
}
}