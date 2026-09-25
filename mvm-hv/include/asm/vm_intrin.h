#pragma once
#include <cstdint>

#pragma pack(push, 1)
struct svm_descriptor_table_t
{
    uint16_t limit;
    uint64_t base;
};
#pragma pack(pop)
static_assert(sizeof(svm_descriptor_table_t) == 10, "svm_descriptor_table_t must be 10 bytes");

extern "C"
{
    void svm_clgi();
    void svm_stgi();
    void svm_vmload(uint64_t vmcb_physical_address);
    void svm_vmsave(uint64_t vmcb_physical_address);

    uint16_t svm_read_cs();
    uint16_t svm_read_ss();
    uint16_t svm_read_ds();
    uint16_t svm_read_es();
    uint16_t svm_read_fs();
    uint16_t svm_read_gs();
    uint16_t svm_read_ldtr();
    uint16_t svm_read_tr();
    void     svm_sgdt(svm_descriptor_table_t* out);
    void     svm_sidt(svm_descriptor_table_t* out);
    uint64_t svm_load_ar(uint64_t selector);

    void svm_lgdt(const svm_descriptor_table_t* in);
    void svm_ltr(uint16_t selector);
    void svm_lidt(const svm_descriptor_table_t* in);
}
