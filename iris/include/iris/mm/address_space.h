#pragma once

#include <di/prelude.h>
#include <iris/core/error.h>
#include <iris/mm/physical_address.h>
#include <iris/mm/virtual_address.h>

namespace iris::mm {
class AddressSpace {
public:
    explicit AddressSpace(u64 architecture_page_table_base) : m_architecture_page_table_base(architecture_page_table_base) {}

    u64 architecture_page_table_base() const { return m_architecture_page_table_base; }

    Expected<void> map_physical_page(VirtualAddress location, PhysicalAddress physical_address);

private:
    u64 m_architecture_page_table_base;
};
}