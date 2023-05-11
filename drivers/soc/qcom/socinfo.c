// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2009-2017, The Linux Foundation. All rights reserved.
 * Copyright (c) 2017-2019, Linaro Ltd.
 */

#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/soc/qcom/smem.h>
#include <linux/string.h>
#include <linux/sys_soc.h>
#include <linux/types.h>

#include <asm/system_misc.h>

#include <soc/qcom/socinfo.h>
#include <linux/soc/qcom/smem.h>
#include <soc/qcom/boot_stats.h>

#define BUILD_ID_LENGTH 32
#define CHIP_ID_LENGTH 32
#define SMEM_IMAGE_VERSION_BLOCKS_COUNT 32
#define SMEM_IMAGE_VERSION_SINGLE_BLOCK_SIZE 128
#define SMEM_IMAGE_VERSION_SIZE 4096
#define SMEM_IMAGE_VERSION_NAME_SIZE 75
#define SMEM_IMAGE_VERSION_VARIANT_SIZE 20
#define SMEM_IMAGE_VERSION_VARIANT_OFFSET 75
#define SMEM_IMAGE_VERSION_OEM_SIZE 33
#define SMEM_IMAGE_VERSION_OEM_OFFSET 95
#define SMEM_IMAGE_VERSION_PARTITION_APPS 10

static DECLARE_RWSEM(current_image_rwsem);
enum {
	HW_PLATFORM_UNKNOWN = 0,
	HW_PLATFORM_SURF    = 1,
	HW_PLATFORM_FFA     = 2,
	HW_PLATFORM_FLUID   = 3,
	HW_PLATFORM_SVLTE_FFA	= 4,
	HW_PLATFORM_SVLTE_SURF	= 5,
	HW_PLATFORM_MTP_MDM = 7,
	HW_PLATFORM_MTP  = 8,
	HW_PLATFORM_LIQUID  = 9,
	/* Dragonboard platform id is assigned as 10 in CDT */
	HW_PLATFORM_DRAGON	= 10,
	HW_PLATFORM_QRD	= 11,
	HW_PLATFORM_HRD	= 13,
	HW_PLATFORM_DTV	= 14,
	HW_PLATFORM_RCM	= 21,
	HW_PLATFORM_STP = 23,
	HW_PLATFORM_SBC = 24,
	HW_PLATFORM_HDK = 31,
	HW_PLATFORM_IDP = 34,
	HW_PLATFORM_INVALID
};

const char *hw_platform[] = {
	[HW_PLATFORM_UNKNOWN] = "Unknown",
	[HW_PLATFORM_SURF] = "Surf",
	[HW_PLATFORM_FFA] = "FFA",
	[HW_PLATFORM_FLUID] = "Fluid",
	[HW_PLATFORM_SVLTE_FFA] = "SVLTE_FFA",
	[HW_PLATFORM_SVLTE_SURF] = "SLVTE_SURF",
	[HW_PLATFORM_MTP_MDM] = "MDM_MTP_NO_DISPLAY",
	[HW_PLATFORM_MTP] = "MTP",
	[HW_PLATFORM_RCM] = "RCM",
	[HW_PLATFORM_LIQUID] = "Liquid",
	[HW_PLATFORM_DRAGON] = "Dragon",
	[HW_PLATFORM_QRD] = "QRD",
	[HW_PLATFORM_HRD] = "HRD",
	[HW_PLATFORM_DTV] = "DTV",
	[HW_PLATFORM_STP] = "STP",
	[HW_PLATFORM_SBC] = "SBC",
	[HW_PLATFORM_HDK] = "HDK",
	[HW_PLATFORM_IDP] = "IDP"
};

enum {
	ACCESSORY_CHIP_UNKNOWN = 0,
	ACCESSORY_CHIP_CHARM = 58,
};

enum {
	PLATFORM_SUBTYPE_QRD = 0x0,
	PLATFORM_SUBTYPE_SKUAA = 0x1,
	PLATFORM_SUBTYPE_SKUF = 0x2,
	PLATFORM_SUBTYPE_SKUAB = 0x3,
	PLATFORM_SUBTYPE_SKUG = 0x5,
	PLATFORM_SUBTYPE_QRD_INVALID,
};

const char *qrd_hw_platform_subtype[] = {
	[PLATFORM_SUBTYPE_QRD] = "QRD",
	[PLATFORM_SUBTYPE_SKUAA] = "SKUAA",
	[PLATFORM_SUBTYPE_SKUF] = "SKUF",
	[PLATFORM_SUBTYPE_SKUAB] = "SKUAB",
	[PLATFORM_SUBTYPE_SKUG] = "SKUG",
	[PLATFORM_SUBTYPE_QRD_INVALID] = "INVALID",
};

enum {
	PLATFORM_SUBTYPE_UNKNOWN = 0x0,
	PLATFORM_SUBTYPE_CHARM = 0x1,
	PLATFORM_SUBTYPE_STRANGE = 0x2,
	PLATFORM_SUBTYPE_STRANGE_2A = 0x3,
	PLATFORM_SUBTYPE_INVALID,
};

const char *hw_platform_subtype[] = {
	[PLATFORM_SUBTYPE_UNKNOWN] = "Unknown",
	[PLATFORM_SUBTYPE_CHARM] = "charm",
	[PLATFORM_SUBTYPE_STRANGE] = "strange",
	[PLATFORM_SUBTYPE_STRANGE_2A] = "strange_2a",
	[PLATFORM_SUBTYPE_INVALID] = "Invalid",
};

/* Used to parse shared memory.  Must match the modem. */
struct socinfo_v0_1 {
	uint32_t format;
	uint32_t id;
	uint32_t version;
	char build_id[BUILD_ID_LENGTH];
};

struct socinfo_v0_2 {
	struct socinfo_v0_1 v0_1;
	uint32_t raw_id;
	uint32_t raw_version;
};

struct socinfo_v0_3 {
	struct socinfo_v0_2 v0_2;
	uint32_t hw_platform;
};

struct socinfo_v0_4 {
	struct socinfo_v0_3 v0_3;
	uint32_t platform_version;
};

struct socinfo_v0_5 {
	struct socinfo_v0_4 v0_4;
	uint32_t accessory_chip;
};

struct socinfo_v0_6 {
	struct socinfo_v0_5 v0_5;
	uint32_t hw_platform_subtype;
};

struct socinfo_v0_7 {
	struct socinfo_v0_6 v0_6;
	uint32_t pmic_model;
	uint32_t pmic_die_revision;
};

struct socinfo_v0_8 {
	struct socinfo_v0_7 v0_7;
	uint32_t pmic_model_1;
	uint32_t pmic_die_revision_1;
	uint32_t pmic_model_2;
	uint32_t pmic_die_revision_2;
};

struct socinfo_v0_9 {
	struct socinfo_v0_8 v0_8;
	uint32_t foundry_id;
};

struct socinfo_v0_10 {
	struct socinfo_v0_9 v0_9;
	uint32_t serial_number;
};

struct socinfo_v0_11 {
	struct socinfo_v0_10 v0_10;
	uint32_t num_pmics;
	uint32_t pmic_array_offset;
};

struct socinfo_v0_12 {
	struct socinfo_v0_11 v0_11;
	uint32_t chip_family;
	uint32_t raw_device_family;
	uint32_t raw_device_number;
};

struct socinfo_v0_13 {
	struct socinfo_v0_12 v0_12;
	uint32_t nproduct_id;
	char chip_name[CHIP_ID_LENGTH];
};

struct socinfo_v0_14 {
	struct socinfo_v0_13 v0_13;
	uint32_t num_clusters;
	uint32_t ncluster_array_offset;
	uint32_t num_defective_parts;
	uint32_t ndefective_parts_array_offset;
};

struct socinfo_v0_15 {
	struct socinfo_v0_14 v0_14;
	uint32_t nmodem_supported;
};

static union {
	struct socinfo_v0_1 v0_1;
	struct socinfo_v0_2 v0_2;
	struct socinfo_v0_3 v0_3;
	struct socinfo_v0_4 v0_4;
	struct socinfo_v0_5 v0_5;
	struct socinfo_v0_6 v0_6;
	struct socinfo_v0_7 v0_7;
	struct socinfo_v0_8 v0_8;
	struct socinfo_v0_9 v0_9;
	struct socinfo_v0_10 v0_10;
	struct socinfo_v0_11 v0_11;
	struct socinfo_v0_12 v0_12;
	struct socinfo_v0_13 v0_13;
	struct socinfo_v0_14 v0_14;
	struct socinfo_v0_15 v0_15;
} *socinfo;

/* max socinfo format version supported */
#define MAX_SOCINFO_FORMAT SOCINFO_VERSION(0, 15)

static struct msm_soc_info cpu_of_id[] = {
	[0]  = {MSM_CPU_UNKNOWN, "Unknown CPU"},
	/* 8960 IDs */
	[87] = {MSM_CPU_8960, "MSM8960"},

	/* 8064 IDs */
	[109] = {MSM_CPU_8064, "APQ8064"},
	[122] = {MSM_CPU_8960, "MSM8960"},

	/* 8260A ID */
	[123] = {MSM_CPU_8960, "MSM8960"},

	/* 8060A ID */
	[124] = {MSM_CPU_8960, "MSM8960"},

	/* 8064 MPQ ID */
	[130] = {MSM_CPU_8064, "APQ8064"},

	/* 8974 IDs */
	[126] = {MSM_CPU_8974, "MSM8974"},
	[184] = {MSM_CPU_8974, "MSM8974"},
	[185] = {MSM_CPU_8974, "MSM8974"},
	[186] = {MSM_CPU_8974, "MSM8974"},

	/* 8974AA IDs */
	[208] = {MSM_CPU_8974PRO_AA, "MSM8974PRO-AA"},
	[211] = {MSM_CPU_8974PRO_AA, "MSM8974PRO-AA"},
	[214] = {MSM_CPU_8974PRO_AA, "MSM8974PRO-AA"},
	[217] = {MSM_CPU_8974PRO_AA, "MSM8974PRO-AA"},

	/* 8974AB IDs */
	[209] = {MSM_CPU_8974PRO_AB, "MSM8974PRO-AB"},
	[212] = {MSM_CPU_8974PRO_AB, "MSM8974PRO-AB"},
	[215] = {MSM_CPU_8974PRO_AB, "MSM8974PRO-AB"},
	[218] = {MSM_CPU_8974PRO_AB, "MSM8974PRO-AB"},

	/* 8974AC IDs */
	[194] = {MSM_CPU_8974PRO_AC, "MSM8974PRO-AC"},
	[210] = {MSM_CPU_8974PRO_AC, "MSM8974PRO-AC"},
	[213] = {MSM_CPU_8974PRO_AC, "MSM8974PRO-AC"},
	[216] = {MSM_CPU_8974PRO_AC, "MSM8974PRO-AC"},

	/* 8960AB IDs */
	[138] = {MSM_CPU_8960AB, "MSM8960AB"},
	[139] = {MSM_CPU_8960AB, "MSM8960AB"},
	[140] = {MSM_CPU_8960AB, "MSM8960AB"},
	[141] = {MSM_CPU_8960AB, "MSM8960AB"},

	/* 8084 IDs */
	[178] = {MSM_CPU_8084, "APQ8084"},

	/* 8916 IDs */
	[206] = {MSM_CPU_8916, "MSM8916"},
	[247] = {MSM_CPU_8916, "APQ8016"},
	[248] = {MSM_CPU_8916, "MSM8216"},
	[249] = {MSM_CPU_8916, "MSM8116"},
	[250] = {MSM_CPU_8916, "MSM8616"},

	/* 8996 IDs */
	[246] = {MSM_CPU_8996, "MSM8996"},
	[310] = {MSM_CPU_8996, "MSM8996"},
	[311] = {MSM_CPU_8996, "APQ8096"},
	[291] = {MSM_CPU_8996, "APQ8096"},
	[305] = {MSM_CPU_8996, "MSM8996pro"},
	[312] = {MSM_CPU_8996, "APQ8096pro"},

	/* SDM660 ID */
	[317] = {MSM_CPU_SDM660, "SDM660"},

	/* sm8150 ID */
	[339] = {MSM_CPU_SM8150, "SM8150"},

	/* sa8150 ID */
	[362] = {MSM_CPU_SA8150, "SA8150"},

	/* sdmshrike ID */
	[340] = {MSM_CPU_SDMSHRIKE, "SDMSHRIKE"},

	/* sm6150 ID */
	[355] = {MSM_CPU_SM6150, "SM6150"},

	/* qcs405 ID */
	[352] = {MSM_CPU_QCS405, "QCS405"},

	/* sdxprairie ID */
	[357] = {SDX_CPU_SDXPRAIRIE, "SDXPRAIRIE"},

	/* sdmmagpie ID */
	[365] = {MSM_CPU_SDMMAGPIE, "SDMMAGPIE"},

	/* kona ID */
	[356] = {MSM_CPU_KONA, "KONA"},
	[455] = {MSM_CPU_KONA, "KONA"},
	[496] = {MSM_CPU_KONA, "KONA"},

	/* Lito ID */
	[400] = {MSM_CPU_LITO, "LITO"},
	[440] = {MSM_CPU_LITO, "LITO"},

	/* Orchid ID */
	[476] = {MSM_CPU_ORCHID, "ORCHID"},

	/* Bengal ID */
	[417] = {MSM_CPU_BENGAL, "BENGAL"},
	[444] = {MSM_CPU_BENGAL, "BENGAL"},

	/* Khaje ID */
	[518] = {MSM_CPU_KHAJE, "SM6225"},

	/* Lagoon ID */
	[434] = {MSM_CPU_LAGOON, "LAGOON"},
	[459] = {MSM_CPU_LAGOON, "LAGOON"},

	/* Bengalp ID */
	[445] = {MSM_CPU_BENGALP, "BENGALP"},
	[420] = {MSM_CPU_BENGALP, "BENGALP"},

	/* Scuba ID */
	[441] = {MSM_CPU_SCUBA, "SCUBA"},
	[471] = {MSM_CPU_SCUBA, "SCUBA"},

	/* Scuba IIOT  ID */
	[473] = {MSM_CPU_SCUBAIOT, "SCUBAIIOT"},
	[474] = {MSM_CPU_SCUBAPIOT, "SCUBAPIIOT"},

	/* BENGAL-IOT ID */
	[469] = {MSM_CPU_BENGAL_IOT, "BENGAL-IOT"},

	/* BENGALP-IOT ID */
	[470] = {MSM_CPU_BENGALP_IOT, "BENGALP-IOT"},

	/* MSM8937 ID */
	[294] = {MSM_CPU_8937, "MSM8937"},
	[295] = {MSM_CPU_8937, "APQ8937"},

	/* MSM8917 IDs */
	[303] = {MSM_CPU_8917, "MSM8917"},
	[307] = {MSM_CPU_8917, "APQ8017"},
	[308] = {MSM_CPU_8917, "MSM8217"},
	[309] = {MSM_CPU_8917, "MSM8617"},

	/* SDM429 and SDM439 ID */
	[353] = {MSM_CPU_SDM439, "SDM439"},
	[354] = {MSM_CPU_SDM429, "SDM429"},


	/* QM215 ID */
	[386] = {MSM_CPU_QM215, "QM215"},

	/* 8953 ID */
	[293] = {MSM_CPU_8953, "MSM8953"},
	[304] = {MSM_CPU_8953, "APQ8053"},

	/* SDM450 ID */
	[338] = {MSM_CPU_SDM450, "SDM450"},

	/* Uninitialized IDs are not known to run Linux.
	 * MSM_CPU_UNKNOWN is set to 0 to ensure these IDs are
	 * considered as unknown CPU.
	 */
};

static enum msm_cpu cur_cpu;
static int current_image;
static uint32_t socinfo_format;

static struct socinfo_v0_1 dummy_socinfo = {
	.format = SOCINFO_VERSION(0, 1),
	.version = 1,
};

uint32_t socinfo_get_id(void)
{
	return (socinfo) ? socinfo->v0_1.id : 0;
}
EXPORT_SYMBOL(socinfo_get_id);

char *socinfo_get_id_string(void)
{
	return (socinfo) ? cpu_of_id[socinfo->v0_1.id].soc_id_string : NULL;
}
EXPORT_SYMBOL(socinfo_get_id_string);

uint32_t socinfo_get_version(void)
{
	return (socinfo) ? socinfo->v0_1.version : 0;
}

char *socinfo_get_build_id(void)
{
	return (socinfo) ? socinfo->v0_1.build_id : NULL;
}

static char *msm_read_hardware_id(void)
{
	static char msm_soc_str[256] = "Qualcomm Technologies, Inc ";
	static bool string_generated;
	int ret = 0;

	if (string_generated)
		return msm_soc_str;
	if (!socinfo)
		goto err_path;
	if (!cpu_of_id[socinfo->v0_1.id].soc_id_string)
		goto err_path;

	ret = strlcat(msm_soc_str, cpu_of_id[socinfo->v0_1.id].soc_id_string,
			sizeof(msm_soc_str));
	if (ret > sizeof(msm_soc_str))
		goto err_path;

	string_generated = true;
	return msm_soc_str;
err_path:
	return "UNKNOWN SOC TYPE";
}

const char * __init arch_read_machine_name(void)
{
	static char msm_machine_name[256] = "Qualcomm Technologies, Inc. ";
	static bool string_generated;
	u32 len = 0;
	const char *name;

	if (string_generated)
		return msm_machine_name;

	len = strlen(msm_machine_name);
	name = of_get_flat_dt_prop(of_get_flat_dt_root(),
				"qcom,msm-name", NULL);
	if (name)
		len += snprintf(msm_machine_name + len,
					sizeof(msm_machine_name) - len,
					"%s", name);
	else
		goto no_prop_path;

	name = of_get_flat_dt_prop(of_get_flat_dt_root(),
				"qcom,pmic-name", NULL);
	if (name) {
		len += snprintf(msm_machine_name + len,
					sizeof(msm_machine_name) - len,
					"%s", " ");
		len += snprintf(msm_machine_name + len,
					sizeof(msm_machine_name) - len,
					"%s", name);
	} else
		goto no_prop_path;

	name = of_flat_dt_get_machine_name();
	if (name) {
		len += snprintf(msm_machine_name + len,
					sizeof(msm_machine_name) - len,
					"%s", " ");
		len += snprintf(msm_machine_name + len,
					sizeof(msm_machine_name) - len,
					"%s", name);
	} else
		goto no_prop_path;

	string_generated = true;
	return msm_machine_name;
no_prop_path:
	return of_flat_dt_get_machine_name();
}

uint32_t socinfo_get_raw_id(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 2) ?
			socinfo->v0_2.raw_id : 0)
		: 0;
}

uint32_t socinfo_get_raw_version(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 2) ?
			socinfo->v0_2.raw_version : 0)
		: 0;
}

uint32_t socinfo_get_platform_type(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 3) ?
			socinfo->v0_3.hw_platform : 0)
		: 0;
}


uint32_t socinfo_get_platform_version(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 4) ?
			socinfo->v0_4.platform_version : 0)
		: 0;
}

/* This information is directly encoded by the machine id */
/* Thus no external callers rely on this information at the moment */
static uint32_t socinfo_get_accessory_chip(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 5) ?
			socinfo->v0_5.accessory_chip : 0)
		: 0;
}

uint32_t socinfo_get_platform_subtype(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 6) ?
			socinfo->v0_6.hw_platform_subtype : 0)
		: 0;
}

static uint32_t socinfo_get_foundry_id(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 9) ?
			socinfo->v0_9.foundry_id : 0)
		: 0;
}

uint32_t socinfo_get_serial_number(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 10) ?
			socinfo->v0_10.serial_number : 0)
		: 0;
}
EXPORT_SYMBOL(socinfo_get_serial_number);

static uint32_t socinfo_get_chip_family(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 12) ?
			socinfo->v0_12.chip_family : 0)
		: 0;
}

static uint32_t socinfo_get_raw_device_family(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 12) ?
			socinfo->v0_12.raw_device_family : 0)
		: 0;
}

static uint32_t socinfo_get_raw_device_number(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 12) ?
			socinfo->v0_12.raw_device_number : 0)
		: 0;
}

static char *socinfo_get_chip_name(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 13) ?
			socinfo->v0_13.chip_name : "N/A")
		: "N/A";
}

static uint32_t socinfo_get_nproduct_id(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 13) ?
			socinfo->v0_13.nproduct_id : 0)
		: 0;
}

static uint32_t socinfo_get_num_clusters(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 14) ?
			socinfo->v0_14.num_clusters : 0)
		: 0;
}

static uint32_t socinfo_get_ncluster_array_offset(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 14) ?
			socinfo->v0_14.ncluster_array_offset : 0)
		: 0;
}

static uint32_t socinfo_get_num_defective_parts(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 14) ?
			socinfo->v0_14.num_defective_parts : 0)
		: 0;
}

static uint32_t socinfo_get_ndefective_parts_array_offset(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 14) ?
			socinfo->v0_14.ndefective_parts_array_offset : 0)
		: 0;
}

static uint32_t socinfo_get_nmodem_supported(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 15) ?
			socinfo->v0_15.nmodem_supported : 0)
		: 0;
}

enum pmic_model socinfo_get_pmic_model(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 7) ?
			socinfo->v0_7.pmic_model : PMIC_MODEL_UNKNOWN)
		: PMIC_MODEL_UNKNOWN;
}

uint32_t socinfo_get_pmic_die_revision(void)
{
	return socinfo ?
		(socinfo_format >= SOCINFO_VERSION(0, 7) ?
			socinfo->v0_7.pmic_die_revision : 0)
		: 0;
}

static char *socinfo_get_image_version_base_address(void)
{
	size_t size;

	return qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_IMAGE_VERSION_TABLE,
			&size);
}

enum msm_cpu socinfo_get_msm_cpu(void)
{
	return cur_cpu;
}
EXPORT_SYMBOL(socinfo_get_msm_cpu);

static ssize_t
msm_get_vendor(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return snprintf(buf, PAGE_SIZE, "Qualcomm Technologies, Inc\n");
}

static ssize_t
msm_get_raw_id(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n",
		socinfo_get_raw_id());
}

static ssize_t
msm_get_raw_version(struct device *dev,
		     struct device_attribute *attr,
		     char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n",
		socinfo_get_raw_version());
}

static ssize_t
msm_get_build_id(struct device *dev,
		   struct device_attribute *attr,
		   char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%-.32s\n",
			socinfo_get_build_id());
}

static ssize_t
msm_get_hw_platform(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	uint32_t hw_type;

	hw_type = socinfo_get_platform_type();

	return snprintf(buf, PAGE_SIZE, "%-.32s\n",
			hw_platform[hw_type]);
}

static ssize_t
msm_get_platform_version(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n",
		socinfo_get_platform_version());
}

static ssize_t
msm_get_accessory_chip(struct device *dev,
				struct device_attribute *attr,
				char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n",
		socinfo_get_accessory_chip());
}

static ssize_t
msm_get_platform_subtype(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	uint32_t hw_subtype;

	hw_subtype = socinfo_get_platform_subtype();
	if (socinfo_get_platform_type() == HW_PLATFORM_QRD) {
		if (hw_subtype >= PLATFORM_SUBTYPE_QRD_INVALID) {
			pr_err("Invalid hardware platform sub type for qrd found\n");
			hw_subtype = PLATFORM_SUBTYPE_QRD_INVALID;
		}
		return snprintf(buf, PAGE_SIZE, "%-.32s\n",
					qrd_hw_platform_subtype[hw_subtype]);
	} else {
		if (hw_subtype >= PLATFORM_SUBTYPE_INVALID) {
			pr_err("Invalid hardware platform subtype\n");
			hw_subtype = PLATFORM_SUBTYPE_INVALID;
		}
		return snprintf(buf, PAGE_SIZE, "%-.32s\n",
			hw_platform_subtype[hw_subtype]);
	}
}

static ssize_t
msm_get_platform_subtype_id(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	uint32_t hw_subtype;

	hw_subtype = socinfo_get_platform_subtype();
	return snprintf(buf, PAGE_SIZE, "%u\n",
		hw_subtype);
}

static ssize_t
msm_get_foundry_id(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n",
		socinfo_get_foundry_id());
}

static ssize_t
msm_get_serial_number(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n",
		socinfo_get_serial_number());
}

static ssize_t
msm_get_chip_family(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%x\n",
		socinfo_get_chip_family());
}

static ssize_t
msm_get_raw_device_family(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%x\n",
		socinfo_get_raw_device_family());
}

static ssize_t
msm_get_raw_device_number(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%x\n",
		socinfo_get_raw_device_number());
}

static ssize_t
msm_get_chip_name(struct device *dev,
		   struct device_attribute *attr,
		   char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%-.32s\n",
			socinfo_get_chip_name());
}

static ssize_t
msm_get_nproduct_id(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%x\n",
		socinfo_get_nproduct_id());
}

static ssize_t
msm_get_num_clusters(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%x\n",
		socinfo_get_num_clusters());
}

static ssize_t
msm_get_ncluster_array_offset(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%x\n",
		socinfo_get_ncluster_array_offset());
}

static ssize_t
msm_get_num_defective_parts(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%x\n",
		socinfo_get_num_defective_parts());
}

static ssize_t
msm_get_ndefective_parts_array_offset(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%x\n",
		socinfo_get_ndefective_parts_array_offset());
}

static ssize_t
msm_get_nmodem_supported(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE, "0x%x\n",
		socinfo_get_nmodem_supported());
}

static ssize_t
msm_get_pmic_model(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n",
		socinfo_get_pmic_model());
}

static ssize_t
msm_get_pmic_die_revision(struct device *dev,
			       struct device_attribute *attr,
			       char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%u\n",
			 socinfo_get_pmic_die_revision());
}

static ssize_t
msm_get_image_version(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	char *string_address;

	string_address = socinfo_get_image_version_base_address();
	if (IS_ERR_OR_NULL(string_address)) {
		pr_err("Failed to get image version base address");
		return snprintf(buf, SMEM_IMAGE_VERSION_NAME_SIZE, "Unknown");
	}
	down_read(&current_image_rwsem);
	string_address += current_image * SMEM_IMAGE_VERSION_SINGLE_BLOCK_SIZE;
	up_read(&current_image_rwsem);
	return snprintf(buf, SMEM_IMAGE_VERSION_NAME_SIZE, "%-.75s\n",
			string_address);
}

static ssize_t
msm_set_image_version(struct device *dev,
			struct device_attribute *attr,
			const char *buf,
			size_t count)
{
	char *store_address;

	down_read(&current_image_rwsem);
	if (current_image != SMEM_IMAGE_VERSION_PARTITION_APPS) {
		up_read(&current_image_rwsem);
		return count;
	}
	store_address = socinfo_get_image_version_base_address();
	if (IS_ERR_OR_NULL(store_address)) {
		pr_err("Failed to get image version base address");
		up_read(&current_image_rwsem);
		return count;
	}
	store_address += current_image * SMEM_IMAGE_VERSION_SINGLE_BLOCK_SIZE;
	up_read(&current_image_rwsem);
	snprintf(store_address, SMEM_IMAGE_VERSION_NAME_SIZE, "%-.75s", buf);
	return count;
}

static ssize_t
msm_get_image_variant(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	char *string_address;

	string_address = socinfo_get_image_version_base_address();
	if (IS_ERR_OR_NULL(string_address)) {
		pr_err("Failed to get image version base address");
		return snprintf(buf, SMEM_IMAGE_VERSION_VARIANT_SIZE,
		"Unknown");
	}
	down_read(&current_image_rwsem);
	string_address += current_image * SMEM_IMAGE_VERSION_SINGLE_BLOCK_SIZE;
	up_read(&current_image_rwsem);
	string_address += SMEM_IMAGE_VERSION_VARIANT_OFFSET;
	return snprintf(buf, SMEM_IMAGE_VERSION_VARIANT_SIZE, "%-.20s\n",
			string_address);
}

static ssize_t
msm_set_image_variant(struct device *dev,
			struct device_attribute *attr,
			const char *buf,
			size_t count)
{
	char *store_address;

	down_read(&current_image_rwsem);
	if (current_image != SMEM_IMAGE_VERSION_PARTITION_APPS) {
		up_read(&current_image_rwsem);
		return count;
	}
	store_address = socinfo_get_image_version_base_address();
	if (IS_ERR_OR_NULL(store_address)) {
		pr_err("Failed to get image version base address");
		up_read(&current_image_rwsem);
		return count;
	}
	store_address += current_image * SMEM_IMAGE_VERSION_SINGLE_BLOCK_SIZE;
	up_read(&current_image_rwsem);
	store_address += SMEM_IMAGE_VERSION_VARIANT_OFFSET;
	snprintf(store_address, SMEM_IMAGE_VERSION_VARIANT_SIZE, "%-.20s", buf);
	return count;
}

static ssize_t
msm_get_image_crm_version(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	char *string_address;

	string_address = socinfo_get_image_version_base_address();
	if (IS_ERR_OR_NULL(string_address)) {
		pr_err("Failed to get image version base address");
		return snprintf(buf, SMEM_IMAGE_VERSION_OEM_SIZE, "Unknown");
	}
	down_read(&current_image_rwsem);
	string_address += current_image * SMEM_IMAGE_VERSION_SINGLE_BLOCK_SIZE;
	up_read(&current_image_rwsem);
	string_address += SMEM_IMAGE_VERSION_OEM_OFFSET;
	return snprintf(buf, SMEM_IMAGE_VERSION_OEM_SIZE, "%-.33s\n",
			string_address);
}

static ssize_t
msm_set_image_crm_version(struct device *dev,
			struct device_attribute *attr,
			const char *buf,
			size_t count)
{
	char *store_address;

	down_read(&current_image_rwsem);
	if (current_image != SMEM_IMAGE_VERSION_PARTITION_APPS) {
		up_read(&current_image_rwsem);
		return count;
	}
	store_address = socinfo_get_image_version_base_address();
	if (IS_ERR_OR_NULL(store_address)) {
		pr_err("Failed to get image version base address");
		up_read(&current_image_rwsem);
		return count;
	}
	store_address += current_image * SMEM_IMAGE_VERSION_SINGLE_BLOCK_SIZE;
	up_read(&current_image_rwsem);
	store_address += SMEM_IMAGE_VERSION_OEM_OFFSET;
	snprintf(store_address, SMEM_IMAGE_VERSION_OEM_SIZE, "%-.33s", buf);
	return count;
}

static ssize_t
msm_get_image_number(struct device *dev,
			struct device_attribute *attr,
			char *buf)
{
	int ret;

	down_read(&current_image_rwsem);
	ret = snprintf(buf, PAGE_SIZE, "%d\n",
			current_image);
	up_read(&current_image_rwsem);
	return ret;

}

static ssize_t
msm_select_image(struct device *dev, struct device_attribute *attr,
			const char *buf, size_t count)
{
	int ret, digit;

	ret = kstrtoint(buf, 10, &digit);
	if (ret)
		return ret;
	down_write(&current_image_rwsem);
	if (digit >= 0 && digit < SMEM_IMAGE_VERSION_BLOCKS_COUNT)
		current_image = digit;
	else
		current_image = 0;
	up_write(&current_image_rwsem);
	return count;
}

static ssize_t
msm_get_images(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int pos = 0;
	int image;
	char *image_address;

	image_address = socinfo_get_image_version_base_address();
	if (IS_ERR_OR_NULL(image_address))
		return snprintf(buf, PAGE_SIZE, "Unavailable\n");

	*buf = '\0';
	for (image = 0; image < SMEM_IMAGE_VERSION_BLOCKS_COUNT; image++) {
		if (*image_address == '\0') {
			image_address += SMEM_IMAGE_VERSION_SINGLE_BLOCK_SIZE;
			continue;
		}

		pos += snprintf(buf + pos, PAGE_SIZE - pos, "%d:\n",
			image);
		pos += snprintf(buf + pos, PAGE_SIZE - pos,
			"\tCRM:\t\t%-.75s\n", image_address);
		pos += snprintf(buf + pos, PAGE_SIZE - pos,
			"\tVariant:\t%-.20s\n",
			image_address + SMEM_IMAGE_VERSION_VARIANT_OFFSET);
		pos += snprintf(buf + pos, PAGE_SIZE - pos,
			"\tVersion:\t%-.33s\n",
			image_address + SMEM_IMAGE_VERSION_OEM_OFFSET);

		image_address += SMEM_IMAGE_VERSION_SINGLE_BLOCK_SIZE;
	}

	return pos;
}

static struct device_attribute msm_soc_attr_raw_version =
	__ATTR(raw_version, 0444, msm_get_raw_version,  NULL);

static struct device_attribute msm_soc_attr_raw_id =
	__ATTR(raw_id, 0444, msm_get_raw_id,  NULL);

static struct device_attribute msm_soc_attr_vendor =
	__ATTR(vendor, 0444, msm_get_vendor,  NULL);

static struct device_attribute msm_soc_attr_build_id =
	__ATTR(build_id, 0444, msm_get_build_id, NULL);

static struct device_attribute msm_soc_attr_hw_platform =
	__ATTR(hw_platform, 0444, msm_get_hw_platform, NULL);


static struct device_attribute msm_soc_attr_platform_version =
	__ATTR(platform_version, 0444,
			msm_get_platform_version, NULL);

static struct device_attribute msm_soc_attr_accessory_chip =
	__ATTR(accessory_chip, 0444,
			msm_get_accessory_chip, NULL);

static struct device_attribute msm_soc_attr_platform_subtype =
	__ATTR(platform_subtype, 0444,
			msm_get_platform_subtype, NULL);

/* Platform Subtype String is being deprecated. Use Platform
 * Subtype ID instead.
 */
#define SOCINFO_MAJOR(ver) (((ver) >> 16) & 0xffff)
#define SOCINFO_MINOR(ver) ((ver) & 0xffff)
#define SOCINFO_VERSION(maj, min)  ((((maj) & 0xffff) << 16)|((min) & 0xffff))

#define SMEM_SOCINFO_BUILD_ID_LENGTH           32
#define SMEM_SOCINFO_CHIP_ID_LENGTH            32

/*
 * SMEM item id, used to acquire handles to respective
 * SMEM region.
 */
#define SMEM_HW_SW_BUILD_ID            137

#ifdef CONFIG_DEBUG_FS
#define SMEM_IMAGE_VERSION_BLOCKS_COUNT        32
#define SMEM_IMAGE_VERSION_SIZE                4096
#define SMEM_IMAGE_VERSION_NAME_SIZE           75
#define SMEM_IMAGE_VERSION_VARIANT_SIZE        20
#define SMEM_IMAGE_VERSION_OEM_SIZE            32

/*
 * SMEM Image table indices
 */
#define SMEM_IMAGE_TABLE_BOOT_INDEX     0
#define SMEM_IMAGE_TABLE_TZ_INDEX       1
#define SMEM_IMAGE_TABLE_RPM_INDEX      3
#define SMEM_IMAGE_TABLE_APPS_INDEX     10
#define SMEM_IMAGE_TABLE_MPSS_INDEX     11
#define SMEM_IMAGE_TABLE_ADSP_INDEX     12
#define SMEM_IMAGE_TABLE_CNSS_INDEX     13
#define SMEM_IMAGE_TABLE_VIDEO_INDEX    14
#define SMEM_IMAGE_VERSION_TABLE       469

/*
 * SMEM Image table names
 */
static const char *const socinfo_image_names[] = {
	[SMEM_IMAGE_TABLE_ADSP_INDEX] = "adsp",
	[SMEM_IMAGE_TABLE_APPS_INDEX] = "apps",
	[SMEM_IMAGE_TABLE_BOOT_INDEX] = "boot",
	[SMEM_IMAGE_TABLE_CNSS_INDEX] = "cnss",
	[SMEM_IMAGE_TABLE_MPSS_INDEX] = "mpss",
	[SMEM_IMAGE_TABLE_RPM_INDEX] = "rpm",
	[SMEM_IMAGE_TABLE_TZ_INDEX] = "tz",
	[SMEM_IMAGE_TABLE_VIDEO_INDEX] = "video",
};

static const char *const pmic_models[] = {
	[0]  = "Unknown PMIC model",
	[9]  = "PM8994",
	[11] = "PM8916",
	[13] = "PM8058",
	[14] = "PM8028",
	[15] = "PM8901",
	[16] = "PM8027",
	[17] = "ISL9519",
	[18] = "PM8921",
	[19] = "PM8018",
	[20] = "PM8015",
	[21] = "PM8014",
	[22] = "PM8821",
	[23] = "PM8038",
	[24] = "PM8922",
	[25] = "PM8917",
};
#endif /* CONFIG_DEBUG_FS */

/* Socinfo SMEM item structure */
struct socinfo {
	__le32 fmt;
	__le32 id;
	__le32 ver;
	char build_id[SMEM_SOCINFO_BUILD_ID_LENGTH];
	/* Version 2 */
	__le32 raw_id;
	__le32 raw_ver;
	/* Version 3 */
	__le32 hw_plat;
	/* Version 4 */
	__le32 plat_ver;
	/* Version 5 */
	__le32 accessory_chip;
	/* Version 6 */
	__le32 hw_plat_subtype;
	/* Version 7 */
	__le32 pmic_model;
	__le32 pmic_die_rev;
	/* Version 8 */
	__le32 pmic_model_1;
	__le32 pmic_die_rev_1;
	__le32 pmic_model_2;
	__le32 pmic_die_rev_2;
	/* Version 9 */
	__le32 foundry_id;
	/* Version 10 */
	__le32 serial_num;
	/* Version 11 */
	__le32 num_pmics;
	__le32 pmic_array_offset;
	/* Version 12 */
	__le32 chip_family;
	__le32 raw_device_family;
	__le32 raw_device_num;
	/* Version 13 */
	__le32 nproduct_id;
	char chip_id[SMEM_SOCINFO_CHIP_ID_LENGTH];
	/* Version 14 */
	__le32 num_clusters;
	__le32 ncluster_array_offset;
	__le32 num_defective_parts;
	__le32 ndefective_parts_array_offset;
	/* Version 15 */
	__le32 nmodem_supported;
};

#ifdef CONFIG_DEBUG_FS
struct socinfo_params {
	u32 raw_device_family;
	u32 hw_plat_subtype;
	u32 accessory_chip;
	u32 raw_device_num;
	u32 chip_family;
	u32 foundry_id;
	u32 plat_ver;
	u32 raw_ver;
	u32 hw_plat;
	u32 fmt;
	u32 nproduct_id;
	u32 num_clusters;
	u32 ncluster_array_offset;
	u32 num_defective_parts;
	u32 ndefective_parts_array_offset;
	u32 nmodem_supported;
};

struct smem_image_version {
	char name[SMEM_IMAGE_VERSION_NAME_SIZE];
	char variant[SMEM_IMAGE_VERSION_VARIANT_SIZE];
	char pad;
	char oem[SMEM_IMAGE_VERSION_OEM_SIZE];
};
#endif /* CONFIG_DEBUG_FS */

struct qcom_socinfo {
	struct soc_device *soc_dev;
	struct soc_device_attribute attr;
#ifdef CONFIG_DEBUG_FS
	struct dentry *dbg_root;
	struct socinfo_params info;
#endif /* CONFIG_DEBUG_FS */
};

struct soc_id {
	unsigned int id;
	const char *name;
};

static const struct soc_id soc_id[] = {
	{ 87, "MSM8960" },
	{ 109, "APQ8064" },
	{ 122, "MSM8660A" },
	{ 123, "MSM8260A" },
	{ 124, "APQ8060A" },
	{ 126, "MSM8974" },
	{ 130, "MPQ8064" },
	{ 138, "MSM8960AB" },
	{ 139, "APQ8060AB" },
	{ 140, "MSM8260AB" },
	{ 141, "MSM8660AB" },
	{ 178, "APQ8084" },
	{ 184, "APQ8074" },
	{ 185, "MSM8274" },
	{ 186, "MSM8674" },
	{ 194, "MSM8974PRO" },
	{ 206, "MSM8916" },
	{ 207, "MSM8994" },
	{ 208, "APQ8074-AA" },
	{ 209, "APQ8074-AB" },
	{ 210, "APQ8074PRO" },
	{ 211, "MSM8274-AA" },
	{ 212, "MSM8274-AB" },
	{ 213, "MSM8274PRO" },
	{ 214, "MSM8674-AA" },
	{ 215, "MSM8674-AB" },
	{ 216, "MSM8674PRO" },
	{ 217, "MSM8974-AA" },
	{ 218, "MSM8974-AB" },
	{ 233, "MSM8936" },
	{ 239, "MSM8939" },
	{ 240, "APQ8036" },
	{ 241, "APQ8039" },
	{ 246, "MSM8996" },
	{ 247, "APQ8016" },
	{ 248, "MSM8216" },
	{ 249, "MSM8116" },
	{ 250, "MSM8616" },
	{ 251, "MSM8992" },
	{ 253, "APQ8094" },
	{ 291, "APQ8096" },
	{ 305, "MSM8996SG" },
	{ 310, "MSM8996AU" },
	{ 311, "APQ8096AU" },
	{ 312, "APQ8096SG" },
	{ 318, "SDM630" },
	{ 321, "SDM845" },
	{ 341, "SDA845" },
	{ 356, "SM8250" },
	{ 402, "IPQ6018" },
	{ 425, "SC7180" },
};

static const char *socinfo_machine(struct device *dev, unsigned int id)
{
	int idx;

	for (idx = 0; idx < ARRAY_SIZE(soc_id); idx++) {
		if (soc_id[idx].id == id)
			return soc_id[idx].name;
	}

	return NULL;
}

#ifdef CONFIG_DEBUG_FS

#define QCOM_OPEN(name, _func)						\
static int qcom_open_##name(struct inode *inode, struct file *file)	\
{									\
	return single_open(file, _func, inode->i_private);		\
}									\
									\
static const struct file_operations qcom_ ##name## _ops = {		\
	.open = qcom_open_##name,					\
	.read = seq_read,						\
	.llseek = seq_lseek,						\
	.release = single_release,					\
}

#define DEBUGFS_ADD(info, name)						\
	debugfs_create_file(__stringify(name), 0400,			\
			    qcom_socinfo->dbg_root,			\
			    info, &qcom_ ##name## _ops)


static int qcom_show_build_id(struct seq_file *seq, void *p)
{
	struct socinfo *socinfo = seq->private;

	seq_printf(seq, "%s\n", socinfo->build_id);

	return 0;
}

static int qcom_show_pmic_model(struct seq_file *seq, void *p)
{
	struct socinfo *socinfo = seq->private;
	int model = SOCINFO_MINOR(le32_to_cpu(socinfo->pmic_model));

	if (model < 0)
		return -EINVAL;

	if (model < ARRAY_SIZE(pmic_models) && pmic_models[model])
		seq_printf(seq, "%s\n", pmic_models[model]);
	else
		seq_printf(seq, "unknown (%d)\n", model);

	return 0;
}

static int qcom_show_pmic_die_revision(struct seq_file *seq, void *p)
{
	struct socinfo *socinfo = seq->private;

	seq_printf(seq, "%u.%u\n",
		   SOCINFO_MAJOR(le32_to_cpu(socinfo->pmic_die_rev)),
		   SOCINFO_MINOR(le32_to_cpu(socinfo->pmic_die_rev)));

	return 0;
}

static int qcom_show_chip_id(struct seq_file *seq, void *p)
{
	struct socinfo *socinfo = seq->private;

	seq_printf(seq, "%s\n", socinfo->chip_id);

	return 0;
}

QCOM_OPEN(build_id, qcom_show_build_id);
QCOM_OPEN(pmic_model, qcom_show_pmic_model);
QCOM_OPEN(pmic_die_rev, qcom_show_pmic_die_revision);
QCOM_OPEN(chip_id, qcom_show_chip_id);

#define DEFINE_IMAGE_OPS(type)					\
static int show_image_##type(struct seq_file *seq, void *p)		  \
{								  \
	struct smem_image_version *image_version = seq->private;  \
	seq_puts(seq, image_version->type);			  \
	seq_putc(seq, '\n');					  \
	return 0;						  \
}								  \
static int open_image_##type(struct inode *inode, struct file *file)	  \
{									  \
	return single_open(file, show_image_##type, inode->i_private); \
}									  \
									  \
static const struct file_operations qcom_image_##type##_ops = {	  \
	.open = open_image_##type,					  \
	.read = seq_read,						  \
	.llseek = seq_lseek,						  \
	.release = single_release,					  \
}

DEFINE_IMAGE_OPS(name);
DEFINE_IMAGE_OPS(variant);
DEFINE_IMAGE_OPS(oem);

static void socinfo_debugfs_init(struct qcom_socinfo *qcom_socinfo,
				 struct socinfo *info)
{
	struct smem_image_version *versions;
	struct dentry *dentry;
	size_t size;
	int i;

	qcom_socinfo->dbg_root = debugfs_create_dir("qcom_socinfo", NULL);

	qcom_socinfo->info.fmt = __le32_to_cpu(info->fmt);

	debugfs_create_x32("info_fmt", 0400, qcom_socinfo->dbg_root,
			   &qcom_socinfo->info.fmt);

	switch (qcom_socinfo->info.fmt) {
	case SOCINFO_VERSION(0, 15):
		qcom_socinfo->info.nmodem_supported = __le32_to_cpu(info->nmodem_supported);

		debugfs_create_u32("nmodem_supported", 0400, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.nmodem_supported);
		fallthrough;
	case SOCINFO_VERSION(0, 14):
		qcom_socinfo->info.num_clusters = __le32_to_cpu(info->num_clusters);
		qcom_socinfo->info.ncluster_array_offset = __le32_to_cpu(info->ncluster_array_offset);
		qcom_socinfo->info.num_defective_parts = __le32_to_cpu(info->num_defective_parts);
		qcom_socinfo->info.ndefective_parts_array_offset = __le32_to_cpu(info->ndefective_parts_array_offset);

		debugfs_create_u32("num_clusters", 0400, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.num_clusters);
		debugfs_create_u32("ncluster_array_offset", 0400, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.ncluster_array_offset);
		debugfs_create_u32("num_defective_parts", 0400, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.num_defective_parts);
		debugfs_create_u32("ndefective_parts_array_offset", 0400, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.ndefective_parts_array_offset);
		fallthrough;
	case SOCINFO_VERSION(0, 13):
		qcom_socinfo->info.nproduct_id = __le32_to_cpu(info->nproduct_id);

		debugfs_create_u32("nproduct_id", 0400, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.nproduct_id);
		DEBUGFS_ADD(info, chip_id);
		fallthrough;
	case SOCINFO_VERSION(0, 12):
		qcom_socinfo->info.chip_family =
			__le32_to_cpu(info->chip_family);
		qcom_socinfo->info.raw_device_family =
			__le32_to_cpu(info->raw_device_family);
		qcom_socinfo->info.raw_device_num =
			__le32_to_cpu(info->raw_device_num);

		debugfs_create_x32("chip_family", 0400, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.chip_family);
		debugfs_create_x32("raw_device_family", 0400,
				   qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.raw_device_family);
		debugfs_create_x32("raw_device_number", 0400,
				   qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.raw_device_num);
		fallthrough;
	case SOCINFO_VERSION(0, 11):
	case SOCINFO_VERSION(0, 10):
	case SOCINFO_VERSION(0, 9):
		qcom_socinfo->info.foundry_id = __le32_to_cpu(info->foundry_id);

		debugfs_create_u32("foundry_id", 0400, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.foundry_id);
		fallthrough;
	case SOCINFO_VERSION(0, 8):
	case SOCINFO_VERSION(0, 7):
		DEBUGFS_ADD(info, pmic_model);
		DEBUGFS_ADD(info, pmic_die_rev);
		fallthrough;
	case SOCINFO_VERSION(0, 6):
		qcom_socinfo->info.hw_plat_subtype =
			__le32_to_cpu(info->hw_plat_subtype);

		debugfs_create_u32("hardware_platform_subtype", 0400,
				   qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.hw_plat_subtype);
		fallthrough;
	case SOCINFO_VERSION(0, 5):
		qcom_socinfo->info.accessory_chip =
			__le32_to_cpu(info->accessory_chip);

		debugfs_create_u32("accessory_chip", 0400,
				   qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.accessory_chip);
		fallthrough;
	case SOCINFO_VERSION(0, 4):
		qcom_socinfo->info.plat_ver = __le32_to_cpu(info->plat_ver);

		debugfs_create_u32("platform_version", 0400,
				   qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.plat_ver);
		fallthrough;
	case SOCINFO_VERSION(0, 3):
		qcom_socinfo->info.hw_plat = __le32_to_cpu(info->hw_plat);

		debugfs_create_u32("hardware_platform", 0400,
				   qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.hw_plat);
		fallthrough;
	case SOCINFO_VERSION(0, 2):
		qcom_socinfo->info.raw_ver  = __le32_to_cpu(info->raw_ver);

		debugfs_create_u32("raw_version", 0400, qcom_socinfo->dbg_root,
				   &qcom_socinfo->info.raw_ver);
		fallthrough;
	case SOCINFO_VERSION(0, 1):
		DEBUGFS_ADD(info, build_id);
		break;
	}

	versions = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_IMAGE_VERSION_TABLE,
				 &size);

	for (i = 0; i < ARRAY_SIZE(socinfo_image_names); i++) {
		if (!socinfo_image_names[i])
			continue;

		dentry = debugfs_create_dir(socinfo_image_names[i],
					    qcom_socinfo->dbg_root);
		debugfs_create_file("name", 0400, dentry, &versions[i],
				    &qcom_image_name_ops);
		debugfs_create_file("variant", 0400, dentry, &versions[i],
				    &qcom_image_variant_ops);
		debugfs_create_file("oem", 0400, dentry, &versions[i],
				    &qcom_image_oem_ops);
	}
}

static void socinfo_debugfs_exit(struct qcom_socinfo *qcom_socinfo)
{
	debugfs_remove_recursive(qcom_socinfo->dbg_root);
}
#else
static void socinfo_debugfs_init(struct qcom_socinfo *qcom_socinfo,
				 struct socinfo *info)
{
}
static void socinfo_debugfs_exit(struct qcom_socinfo *qcom_socinfo) {  }
#endif /* CONFIG_DEBUG_FS */

static int qcom_socinfo_probe(struct platform_device *pdev)
{
	struct qcom_socinfo *qs;
	struct socinfo *info;
	size_t item_size;

	info = qcom_smem_get(QCOM_SMEM_HOST_ANY, SMEM_HW_SW_BUILD_ID,
			      &item_size);
	if (IS_ERR(info)) {
		dev_err(&pdev->dev, "Couldn't find socinfo\n");
		return PTR_ERR(info);
	}

	qs = devm_kzalloc(&pdev->dev, sizeof(*qs), GFP_KERNEL);
	if (!qs)
		return -ENOMEM;

	qs->attr.family = "Snapdragon";
	qs->attr.machine = socinfo_machine(&pdev->dev,
					   le32_to_cpu(info->id));
	qs->attr.soc_id = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%u",
					 le32_to_cpu(info->id));
	qs->attr.revision = devm_kasprintf(&pdev->dev, GFP_KERNEL, "%u.%u",
					   SOCINFO_MAJOR(le32_to_cpu(info->ver)),
					   SOCINFO_MINOR(le32_to_cpu(info->ver)));
	if (offsetof(struct socinfo, serial_num) <= item_size)
		qs->attr.serial_number = devm_kasprintf(&pdev->dev, GFP_KERNEL,
							"%u",
							le32_to_cpu(info->serial_num));

	qs->soc_dev = soc_device_register(&qs->attr);
	if (IS_ERR(qs->soc_dev))
		return PTR_ERR(qs->soc_dev);

	socinfo_debugfs_init(qs, info);

	/* Feed the soc specific unique data into entropy pool */
	add_device_randomness(info, item_size);

	platform_set_drvdata(pdev, qs);

	return 0;
}

static int qcom_socinfo_remove(struct platform_device *pdev)
{
	struct qcom_socinfo *qs = platform_get_drvdata(pdev);

	soc_device_unregister(qs->soc_dev);

	socinfo_debugfs_exit(qs);

	return 0;
}

static struct platform_driver qcom_socinfo_driver = {
	.probe = qcom_socinfo_probe,
	.remove = qcom_socinfo_remove,
	.driver  = {
		.name = "qcom-socinfo",
	},
};

module_platform_driver(qcom_socinfo_driver);

MODULE_DESCRIPTION("Qualcomm SoCinfo driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:qcom-socinfo");
