/* QEMU mps2-an505 (Secure state): code in SSRAM1 seen through its Secure
 * alias at 0x10000000 (init-svtor), data in SSRAM2's Secure alias at
 * 0x38000000. RAM is limited to 256 KiB to match the nRF54L15. */
MEMORY
{
    FLASH (rx)  : ORIGIN = 0x10000000, LENGTH = 1024K
    RAM   (rwx) : ORIGIN = 0x38000000, LENGTH = 256K
}
INCLUDE common.ld
