/* AUTO-GENERATED HEADER FILE FOR SEABIOS FIRMWARE */
/* generated with Linux kernel */
/* search for PARISC_QEMU_MACHINE_HEADER in Linux */

#define PARISC_MODEL "9000/785/C3700"

// this is original c3700:
// #define PARISC_PDC_MODEL 0x5dc0, 0x481, 0x0, 0x2, 0x777c3e84, 0x100000f0, 0x8, 0xb2, 0xb2
// HACK: this is B160L (to avoid kernel crash with old qemu which tries to run 64-bit instruction in sr_disable_hash)
#define PARISC_PDC_MODEL 0x5020, 0x481, 0x0, 0x2020202, 0x7794d7fe, 0x100000f0, 0x4, 0xba, 0xba

#define PARISC_PDC_VERSION 0x0301

#define PARISC_PDC_CPUID 0x026b

#define PARISC_PDC_CAPABILITIES 0x0007

#define PARISC_PDC_ENTRY_ORG 0xf0000018

#define PARISC_PDC_CACHE_INFO \
	0xc0000, 0x91802000, 0x20000, 0x0040, 0x0c00 \
	, 0x0001, 0x180000, 0xb1802000, 0x0000, 0x0040 \
	, 0x6000, 0x0001, 0x00f0, 0xd2300, 0x0000 \
	, 0x0000, 0x0001, 0x0000, 0x0000, 0x0001 \
	, 0x0001, 0x00f0, 0xd2000, 0x0000, 0x0000 \
	, 0x0001, 0x0000, 0x0000, 0x0001, 0x0001


#define HPA_fed00000_DESCRIPTION "Astro BC Runway Port"
static struct pdc_system_map_mod_info mod_info_hpa_fed00000 = {
	.mod_addr = 0xfed00000,
	.mod_pgs = 0x8,
	.add_addrs = 0x0,
};
static struct pdc_module_path mod_path_hpa_fed00000 = {
	.path = { .flags = 0x0, .bc = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }, .mod = 0xa  },
	.layers = { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 }
};
static struct pdc_iodc iodc_data_hpa_fed00000 = {
	.hversion_model = 0x0058,
	.hversion = 0x0020,
	.spa = 0x0000,
	.type = 0x000c,
	.sversion_rev = 0x0000,
	.sversion_model = 0x0005,
	.sversion_opt = 0x0088,
	.rev = 0x0000,
	.dep = 0x0000,
	.features = 0x0000,
	.checksum = 0x0000,
	.length = 0x0000,
	/* pad: 0x0000, 0x0000 */
};
#define HPA_fed00000_num_addr 0
#define HPA_fed00000_add_addr 0


#define HPA_fed30000_DESCRIPTION "Elroy PCI Bridge"
static struct pdc_system_map_mod_info mod_info_hpa_fed30000 = {
	.mod_addr = 0xfed30000,
	.mod_pgs = 0x2,
	.add_addrs = 0x0,
};
static struct pdc_module_path mod_path_hpa_fed30000 = {
	.path = { .flags = 0x0, .bc = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xa }, .mod = 0x0  },
	.layers = { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 }
};
static struct pdc_iodc iodc_data_hpa_fed30000 = {
	.hversion_model = 0x0078,
	.hversion = 0x0020,
	.spa = 0x0000,
	.type = 0x000d,
	.sversion_rev = 0x0000,
	.sversion_model = 0x0005,
	.sversion_opt = 0x0000,
	.rev = 0x0000,
	.dep = 0x0000,
	.features = 0x0000,
	.checksum = 0x0000,
	.length = 0x0000,
	/* pad: 0x0000, 0x0000 */
};
#define HPA_fed30000_num_addr 0
#define HPA_fed30000_add_addr 0


#define HPA_fed32000_DESCRIPTION "Elroy PCI Bridge"
static struct pdc_system_map_mod_info mod_info_hpa_fed32000 = {
	.mod_addr = 0xfed32000,
	.mod_pgs = 0x2,
	.add_addrs = 0x0,
};
static struct pdc_module_path mod_path_hpa_fed32000 = {
	.path = { .flags = 0x0, .bc = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xa }, .mod = 0x1  },
	.layers = { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 }
};
static struct pdc_iodc iodc_data_hpa_fed32000 = {
	.hversion_model = 0x0078,
	.hversion = 0x0020,
	.spa = 0x0000,
	.type = 0x000d,
	.sversion_rev = 0x0000,
	.sversion_model = 0x0005,
	.sversion_opt = 0x0000,
	.rev = 0x0000,
	.dep = 0x0000,
	.features = 0x0000,
	.checksum = 0x0000,
	.length = 0x0000,
	/* pad: 0x0000, 0x0000 */
};
#define HPA_fed32000_num_addr 0
#define HPA_fed32000_add_addr 0


#define HPA_fed38000_DESCRIPTION "Elroy PCI Bridge"
static struct pdc_system_map_mod_info mod_info_hpa_fed38000 = {
	.mod_addr = 0xfed38000,
	.mod_pgs = 0x2,
	.add_addrs = 0x0,
};
static struct pdc_module_path mod_path_hpa_fed38000 = {
	.path = { .flags = 0x0, .bc = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xa }, .mod = 0x4  },
	.layers = { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 }
};
static struct pdc_iodc iodc_data_hpa_fed38000 = {
	.hversion_model = 0x0078,
	.hversion = 0x0020,
	.spa = 0x0000,
	.type = 0x000d,
	.sversion_rev = 0x0000,
	.sversion_model = 0x0005,
	.sversion_opt = 0x0000,
	.rev = 0x0000,
	.dep = 0x0000,
	.features = 0x0000,
	.checksum = 0x0000,
	.length = 0x0000,
	/* pad: 0x0000, 0x0000 */
};
#define HPA_fed38000_num_addr 0
#define HPA_fed38000_add_addr 0


#define HPA_fed3c000_DESCRIPTION "Elroy PCI Bridge"
static struct pdc_system_map_mod_info mod_info_hpa_fed3c000 = {
	.mod_addr = 0xfed3c000,
	.mod_pgs = 0x2,
	.add_addrs = 0x0,
};
static struct pdc_module_path mod_path_hpa_fed3c000 = {
	.path = { .flags = 0x0, .bc = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xa }, .mod = 0x6  },
	.layers = { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 }
};
static struct pdc_iodc iodc_data_hpa_fed3c000 = {
	.hversion_model = 0x0078,
	.hversion = 0x0020,
	.spa = 0x0000,
	.type = 0x000d,
	.sversion_rev = 0x0000,
	.sversion_model = 0x0005,
	.sversion_opt = 0x0000,
	.rev = 0x0000,
	.dep = 0x0000,
	.features = 0x0000,
	.checksum = 0x0000,
	.length = 0x0000,
	/* pad: 0x0000, 0x0000 */
};
#define HPA_fed3c000_num_addr 0
#define HPA_fed3c000_add_addr 0


#define HPA_fffa0000_DESCRIPTION "Allegro W2"
static struct pdc_system_map_mod_info mod_info_hpa_fffa0000 = {
	.mod_addr = CPU_HPA	/* 0xfffa0000 */,
	.mod_pgs = 0x1,
	.add_addrs = 0x0,
};
static struct pdc_module_path mod_path_hpa_fffa0000 = {
	.path = { .flags = 0x0, .bc = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }, .mod = 0x20  },
	.layers = { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 }
};
static struct pdc_iodc iodc_data_hpa_fffa0000 = {
	.hversion_model = 0x005d,
	.hversion = 0x00c0,
	.spa = 0x0000,
	.type = 0x0040,
	.sversion_rev = 0x0000,
	.sversion_model = 0x0002,
	.sversion_opt = 0x0040,
	.rev = 0x0000,
	.dep = 0x0000,
	.features = 0x0000,
	.checksum = 0x0000,
	.length = 0x0000,
	/* pad: 0x0000, 0x0000 */
};
#define HPA_fffa0000_num_addr 0
#define HPA_fffa0000_add_addr 0


#define HPA_fed10200_DESCRIPTION "Memory"
static struct pdc_system_map_mod_info mod_info_hpa_fed10200 = {
	.mod_addr = 0xfed10200,
	.mod_pgs = 0x8,
	.add_addrs = 0x0,
};
static struct pdc_module_path mod_path_hpa_fed10200 = {
	.path = { .flags = 0x0, .bc = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff }, .mod = 0x2f  },
	.layers = { 0x0, 0x0, 0x0, 0x0, 0x0, 0x0 }
};
static struct pdc_iodc iodc_data_hpa_fed10200 = {
	.hversion_model = 0x0009,
	.hversion = 0x00c0,
	.spa = 0x001f,
	.type = 0x0041,
	.sversion_rev = 0x0000,
	.sversion_model = 0x0004,
	.sversion_opt = 0x0080,
	.rev = 0x0000,
	.dep = 0x0000,
	.features = 0x0000,
	.checksum = 0x0000,
	.length = 0x0000,
	/* pad: 0x0000, 0x0000 */
};
#define HPA_fed10200_num_addr 0
#define HPA_fed10200_add_addr 0



#define PARISC_DEVICE_LIST \
	{	.hpa = 0xfed00000,\
		.iodc = &iodc_data_hpa_fed00000,\
		.mod_info = &mod_info_hpa_fed00000,\
		.mod_path = &mod_path_hpa_fed00000,\
		.num_addr = HPA_fed00000_num_addr,\
		.add_addr = { HPA_fed00000_add_addr } },\
	{	.hpa = 0xfed30000,\
		.iodc = &iodc_data_hpa_fed30000,\
		.mod_info = &mod_info_hpa_fed30000,\
		.mod_path = &mod_path_hpa_fed30000,\
		.num_addr = HPA_fed30000_num_addr,\
		.add_addr = { HPA_fed30000_add_addr } },\
	{	.hpa = 0xfed32000,\
		.iodc = &iodc_data_hpa_fed32000,\
		.mod_info = &mod_info_hpa_fed32000,\
		.mod_path = &mod_path_hpa_fed32000,\
		.num_addr = HPA_fed32000_num_addr,\
		.add_addr = { HPA_fed32000_add_addr } },\
	{	.hpa = 0xfed38000,\
		.iodc = &iodc_data_hpa_fed38000,\
		.mod_info = &mod_info_hpa_fed38000,\
		.mod_path = &mod_path_hpa_fed38000,\
		.num_addr = HPA_fed38000_num_addr,\
		.add_addr = { HPA_fed38000_add_addr } },\
	{	.hpa = 0xfed3c000,\
		.iodc = &iodc_data_hpa_fed3c000,\
		.mod_info = &mod_info_hpa_fed3c000,\
		.mod_path = &mod_path_hpa_fed3c000,\
		.num_addr = HPA_fed3c000_num_addr,\
		.add_addr = { HPA_fed3c000_add_addr } },\
	{	.hpa = CPU_HPA    /* XXX: 0xfffa0000 */  ,\
		.iodc = &iodc_data_hpa_fffa0000,\
		.mod_info = &mod_info_hpa_fffa0000,\
		.mod_path = &mod_path_hpa_fffa0000,\
		.num_addr = HPA_fffa0000_num_addr,\
		.add_addr = { HPA_fffa0000_add_addr } },\
	{	.hpa = 0xfed10200,\
		.iodc = &iodc_data_hpa_fed10200,\
		.mod_info = &mod_info_hpa_fed10200,\
		.mod_path = &mod_path_hpa_fed10200,\
		.num_addr = HPA_fed10200_num_addr,\
		.add_addr = { HPA_fed10200_add_addr } },\
/* hacked in from here */   \
	{	.hpa = 0xffd05000,\
		.iodc = &iodc_data_hpa_ffd05000,\
		.mod_info = &mod_info_hpa_ffd05000,\
		.mod_path = &mod_path_hpa_ffd05000,\
		.num_addr = HPA_ffd05000_num_addr,\
		.add_addr = { HPA_ffd05000_add_addr } },\
	{ 0, }
