#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "imp.h"
#include <helper/binarybuffer.h>
#include <helper/time_support.h>
#include <target/algorithm.h>
#include <target/armv7m.h>

/* M4 QSPI REGISTERS*/
volatile uint32_t QSPI_CLK_CONFIG_REG			= 0x12000000;
volatile uint32_t QSPI_BUS_MODE_REG 			= 0x12000004;
volatile uint32_t QSPI_AUTO_CTRL_CONFIG_1_REG    	= 0x12000008;
volatile uint32_t QSPI_AUTO_CTRL_CONFIG_2_REG    	= 0x1200000c;
volatile uint32_t QSPI_MANUAL_CONFIG_REG         	= 0x12000010;
volatile uint32_t QSPI_MANUAL_CONFIG_2_REG       	= 0x12000014;
volatile uint32_t RESERVED_1   		   	 	= 0x12000018;				      
volatile uint32_t QSPI_FIFO_THRESHOLD_REG        	= 0x1200001c;
volatile uint32_t QSPI_STATUS_REG 			= 0x12000020;
volatile uint32_t QSPI_INTR_MASK_REG             	= 0x12000024;
volatile uint32_t QSPI_INTR_UNMASK_REG           	= 0x12000028;
volatile uint32_t QSPI_INTR_STS_REG              	= 0x1200002c;
volatile uint32_t QSPI_INTR_ACK_REG              	= 0x12000030;
volatile uint32_t QSPI_STS_MC_REG                	= 0x12000034;
volatile uint32_t QSPI_AUTO_CONFIG_1_CSN1_REG    	= 0x12000038;
volatile uint32_t QSPI_AUTO_CONFIG_2_CSN1_REG    	= 0x1200003c;
volatile uint32_t QSPI_MANUAL_RD_WR_DATA_REG     	= 0x12000040;
volatile uint32_t QSPI_MANUAL_WRITE_DATA_2_REG       	= 0x12000080;
volatile uint32_t QSPI_AUTO_CONFIG3			= 0x12000090;			
volatile uint32_t QSPI_AUTO_CONFIG3_CSN1		= 0x12000094;
volatile uint32_t OCTA_SPI_BUS_CONTROLLER   		= 0x120000b0;
volatile uint32_t QSPI_AUTO_BASE_ADDR_UNMASK_CSN0	= 0x120000b4;
volatile uint32_t OCTA_SPI_BUS_CONTROLLER2  	 	= 0x120000c4;
volatile uint32_t QSPI_AES_KEY_0_3   		   	= 0x120000c8;
volatile uint32_t QSPI_AES_KEY_4_7   		   	= 0x120000cc;
volatile uint32_t QSPI_AES_KEY_8_B   		   	= 0x120000d0;
volatile uint32_t QSPI_AES_KEY_C_F  		   	= 0x120000d4;
volatile uint32_t QSPI_AES_NONCE_0_3   	   		= 0x120000d8;
volatile uint32_t QSPI_AES_NONCE_4_7   	   		= 0x120000dc;
volatile uint32_t QSPI_AES_NONCE_8_B   	   		= 0x120000e0;
volatile uint32_t QSPI_SEMI_AUTO_ADDR_REG		= 0x1200011c;
volatile uint32_t QSPI_SEMI_AUTO_MODE_CONFIG_REG	= 0x12000120;
volatile uint32_t QSPI_SEMI_AUTO_MODE_CONFIG2_REG	= 0x12000124;
volatile uint32_t QSPI_BUS_MODE2_REG			= 0x12000128;
volatile uint32_t QSPI_AES_SEC_KEY_FRM_KH		= 0x1200012c;
volatile uint32_t QSPI_AUTO_CONITNUE_FETCH_CTRL_REG	= 0x12000130;

#define FLASH_BSY 				(1 << 12)
#define FLASH_BASE				0x8000000
#define	FLASH_ADDR				0x8012000
#define SECTOR_SIZE				0x1000
#define QSPI_BASE				0x12000000

uint32_t flash_offset = 0x12000;


// Private bank information for rs14100.
struct rs14100_flash_bank     
{
  int probed;
};

/* flash_bank rs14100 <base> <size> <chip_width> <bus_width> <target> */
FLASH_BANK_COMMAND_HANDLER(rs14100_flash_bank_command)
{
	struct rs14100_flash_bank *rs14100_info = NULL; 

	if (CMD_ARGC < 6)
		return ERROR_COMMAND_SYNTAX_ERROR;

	rs14100_info = malloc(sizeof(struct rs14100_flash_bank));
	if (rs14100_info == NULL) {
		LOG_ERROR("not enough memory");
		return ERROR_FAIL;
	}

	bank->driver_priv = rs14100_info;

	rs14100_info->probed = 0;

	return ERROR_OK;
}

static int rs14100_init(struct flash_bank *bank)
{
	struct target *target = bank->target;
	struct working_area *init_algorithm;
	struct armv7m_algorithm armv7m_info;
	int retval = ERROR_OK;

	if (target->state != TARGET_HALTED) {
		LOG_ERROR("Target not halted");
		return ERROR_TARGET_NOT_HALTED;
	}


	/* see contrib/loaders/flash/rs14100/rs14100_init.s for src */
	static const uint8_t rs14100_flash_init_code[] = {
		0x4f, 0xf0, 0x00, 0x04, 0xc1, 0xf2, 0x00, 0x24,
  		0x4f, 0xf4, 0x40, 0x66, 0xc0, 0xf2, 0x01, 0x06,
  		0xc4, 0xf8, 0x04, 0x60, 0x4f, 0xf0, 0x0f, 0x06,
  		0xc4, 0xf8, 0xb0, 0x60, 0x4f, 0xf0, 0x00, 0x06,
  		0xc0, 0xf2, 0x02, 0x06, 0xc4, 0xf8, 0xc4, 0x60,
  		0x4f, 0xf0, 0x01, 0x06, 0xc0, 0xf2, 0x28, 0x26,
  		0xc4, 0xf8, 0x10, 0x60, 0x4f, 0xf4, 0x40, 0x66,
  		0xc0, 0xf2, 0x01, 0x06, 0xc4, 0xf8, 0x04, 0x60,
  		0x00, 0xf0, 0x90, 0xf8, 0x4f, 0xf0, 0x08, 0x06,
  		0xc4, 0xf8, 0x80, 0x60, 0x4f, 0xf0, 0xff, 0x06,
  		0xc4, 0xf8, 0x40, 0x60, 0x4f, 0xf0, 0x02, 0x06,
  		0xc0, 0xf2, 0x08, 0x26, 0xc4, 0xf8, 0x10, 0x60,
  		0x00, 0xf0, 0x7b, 0xf8, 0x4f, 0xf0, 0x01, 0x06,
  		0xc0, 0xf2, 0x08, 0x26, 0xc4, 0xf8, 0x10, 0x60,
  		0x4f, 0xf0, 0x01, 0x06, 0xc0, 0xf2, 0x08, 0x26,
  		0xc4, 0xf8, 0x10, 0x60, 0x40, 0xf6, 0x04, 0x46,
  		0xc0, 0xf2, 0x01, 0x06, 0xc4, 0xf8, 0x04, 0x60,
  		0x00, 0xf0, 0x6c, 0xf8, 0x4f, 0xf0, 0x08, 0x06,
 		0xc4, 0xf8, 0x80, 0x60, 0x4f, 0xf0, 0xff, 0x06,
  		0xc4, 0xf8, 0x40, 0x60, 0x4f, 0xf0, 0x02, 0x06,
  		0xc0, 0xf2, 0x08, 0x26, 0xc4, 0xf8, 0x10, 0x60,
  		0x00, 0xf0, 0x57, 0xf8, 0x4f, 0xf0, 0x01, 0x06,
  		0xc0, 0xf2, 0x08, 0x26, 0xc4, 0xf8, 0x10, 0x60,
  		0x4f, 0xf0, 0x01, 0x06, 0xc0, 0xf2, 0x08, 0x26,
  		0xc4, 0xf8, 0x10, 0x60, 0x4f, 0xf4, 0x40, 0x66,
  		0xc0, 0xf2, 0x01, 0x06, 0xc4, 0xf8, 0x04, 0x60,
  		0x4f, 0xf4, 0x80, 0x76, 0xc4, 0xf8, 0x00, 0x60,
  		0x4f, 0xf4, 0x40, 0x66, 0xc0, 0xf2, 0x01, 0x06,
  		0xc4, 0xf8, 0x04, 0x60, 0x4f, 0xf0, 0x01, 0x06,
  		0xc0, 0xf2, 0x08, 0x26, 0xc4, 0xf8, 0x10, 0x60,
  		0x4f, 0xf0, 0xf0, 0x06, 0xc4, 0xf8, 0x14, 0x60,
  		0x4f, 0xf0, 0xf0, 0x06, 0xc4, 0xf8, 0x14, 0x60,
  		0x4f, 0xf0, 0xf0, 0x06, 0xc4, 0xf8, 0x14, 0x60,
  		0x4f, 0xf0, 0x0a, 0x06, 0xc4, 0xf8, 0x30, 0x61,
  		0x4f, 0xf4, 0x40, 0x66, 0xc0, 0xf2, 0x01, 0x06,
  		0xc4, 0xf8, 0x04, 0x60, 0x4f, 0xf0, 0x00, 0x06,
  		0xc4, 0xf8, 0x90, 0x60, 0x4f, 0xf0, 0x00, 0x06,
  		0xc4, 0xf8, 0x90, 0x60, 0x40, 0xf2, 0x11, 0x36,
  		0xc0, 0xf2, 0x03, 0x06, 0xc4, 0xf8, 0x0c, 0x60,
  		0x4f, 0xf0, 0x00, 0x06, 0xc4, 0xf8, 0x08, 0x60,
  		0x4f, 0xf4, 0x40, 0x66, 0xc0, 0xf2, 0x01, 0x06,
  		0xc4, 0xf8, 0x04, 0x60, 0x4f, 0xf4, 0x44, 0x66,
  		0xc0, 0xf2, 0x01, 0x06, 0xc4, 0xf8, 0x04, 0x60,
  		0x09, 0xe0, 0x26, 0x6a, 0x16, 0xf0, 0x01, 0x0f,
 		0xfb, 0xd1, 0x70, 0x47, 0x26, 0x6a, 0x16, 0xf4,
  		0x80, 0x5f, 0xfb, 0xd1, 0x70, 0x47, 0x00, 0xbe,
	};
	

	if (target_alloc_working_area(target, sizeof(rs14100_flash_init_code),
			&init_algorithm) != ERROR_OK) {
		LOG_WARNING("no working area available, can't do block memory writes");
		return ERROR_TARGET_RESOURCE_NOT_AVAILABLE;
	}

	retval = target_write_buffer(target, init_algorithm->address,
			sizeof(rs14100_flash_init_code),
			rs14100_flash_init_code);
	if (retval != ERROR_OK){
		target_free_working_area(target, init_algorithm);
		return retval;
	}

	armv7m_info.common_magic = ARMV7M_COMMON_MAGIC;
	armv7m_info.core_mode = ARM_MODE_THREAD;

	/* Run the algorithm */
	LOG_DEBUG("Running flash init algorithm");
	retval = target_run_algorithm(target, 0 , NULL, 0 , NULL,
		init_algorithm->address,
		init_algorithm->address + sizeof(rs14100_flash_init_code) - 2,
		1000, &armv7m_info);

	if (retval != ERROR_OK)
		LOG_ERROR("Error executing flash init algorithm");

	target_free_working_area(target, init_algorithm);

	return retval;
}

static int rs14100_erase(struct flash_bank *bank, int first, int last)
{
	struct target *target = bank->target;
	struct rs14100_flash_bank *rs14100_info = bank->driver_priv;
	struct reg_param reg_params[2];
	struct working_area *erase_algorithm;
	struct armv7m_algorithm armv7m_info;
	int retval = ERROR_OK;


  	if (target->state != TARGET_HALTED)
	{
    		LOG_ERROR("Target not halted");
    		return ERROR_TARGET_NOT_HALTED;
  	}

 	if ((first < 0) || (last < first) || (last >= bank->num_sectors)) 
  	{
    		LOG_ERROR("Flash sector invalid");
    		return ERROR_FLASH_SECTOR_INVALID;
  	}

  	if (!(rs14100_info->probed)) 
  	{
    		LOG_ERROR("Flash bank not probed");
    		return ERROR_FLASH_BANK_NOT_PROBED;
  	}

	/* see contrib/loaders/flash/rs14100/rs14100_erase.s for src */
	static const uint8_t rs14100_flash_erase_code[] = {
		0x4f, 0xf0, 0x00, 0x04, 0xc1, 0xf2, 0x00, 0x24,
  		0x4f, 0xf4, 0x40, 0x66, 0xc0, 0xf2, 0x01, 0x06,
  		0xc4, 0xf8, 0x04, 0x60, 0x00, 0xf0, 0x5f, 0xf8,
  		0x4f, 0xf0, 0x01, 0x06, 0xc0, 0xf2, 0x08, 0x26,
  		0xc4, 0xf8, 0x10, 0x60, 0x4f, 0xf4, 0x40, 0x66,
  		0xc0, 0xf2, 0x01, 0x06, 0xc4, 0xf8, 0x04, 0x60,
  		0x4f, 0xf4, 0x00, 0x52, 0xc0, 0xf2, 0x01, 0x02,
  		0x4f, 0xf0, 0x08, 0x06, 0xc4, 0xf8, 0x80, 0x60,
  		0x4f, 0xf0, 0x06, 0x06, 0xc4, 0xf8, 0x40, 0x60,
  		0x4f, 0xf0, 0x02, 0x06, 0xc0, 0xf2, 0x08, 0x26,
  		0xc4, 0xf8, 0x10, 0x60, 0x00, 0xf0, 0x32, 0xf8,
  		0x4f, 0xf0, 0x01, 0x06, 0xc0, 0xf2, 0x08, 0x26,
  		0xc4, 0xf8, 0x10, 0x60, 0x4f, 0xf0, 0x08, 0x06,
	 	0xc4, 0xf8, 0x80, 0x60, 0x4f, 0xf0, 0x20, 0x06,
  		0xc4, 0xf8, 0x40, 0x60, 0x4f, 0xf0, 0x02, 0x06,
  		0xc0, 0xf2, 0x08, 0x26, 0xc4, 0xf8, 0x10, 0x60,
  		0x00, 0xf0, 0x1c, 0xf8, 0x4f, 0xf0, 0x18, 0x06,
  		0xc4, 0xf8, 0x80, 0x60, 0x16, 0x46, 0xc4, 0xf8,
  		0x40, 0x60, 0x02, 0xf5, 0x80, 0x52, 0x4f, 0xf0,
  		0x02, 0x06, 0xc0, 0xf2, 0x08, 0x26, 0xc4, 0xf8,
  		0x10, 0x60, 0x00, 0xf0, 0x0b, 0xf8, 0x4f, 0xf0,
  		0x01, 0x06, 0xc0, 0xf2, 0x08, 0x26, 0xc4, 0xf8,
  		0x10, 0x60, 0x00, 0xf0, 0x08, 0xf8, 0x01, 0x39,
  		0x91, 0xb1, 0xbd, 0xe7, 0x26, 0x6a, 0x16, 0xf0,
  		0x01, 0x0f, 0xfb, 0xd1, 0x70, 0x47, 0x4f, 0xf4,
  		0x80, 0x57, 0xc0, 0xf2, 0x10, 0x07, 0x0f, 0xb1,
  		0x01, 0x3f, 0xfc, 0xe7, 0x70, 0x47, 0x26, 0x6a,
  		0x16, 0xf4, 0x00, 0x5f, 0xfb, 0xd1, 0x70, 0x47,
  		0x4f, 0xf0, 0x08, 0x06, 0xc4, 0xf8, 0x80, 0x60,
  		0x4f, 0xf0, 0x04, 0x06, 0xc4, 0xf8, 0x40, 0x60,
  		0x4f, 0xf0, 0x02, 0x06, 0xc0, 0xf2, 0x08, 0x26,
  		0xc4, 0xf8, 0x10, 0x60, 0xff, 0xf7, 0xde, 0xff,
  		0x4f, 0xf0, 0x01, 0x06, 0xc0, 0xf2, 0x08, 0x26,
  		0xc4, 0xf8, 0x10, 0x60, 0x4f, 0xf4, 0x44, 0x66,
  		0xc0, 0xf2, 0x01, 0x06, 0xc4, 0xf8, 0x04, 0x60,
  		0x4f, 0xf0, 0x01, 0x06, 0xc0, 0xf2, 0x08, 0x26,
  		0xc4, 0xf8, 0x10, 0x60, 0x00, 0xbe,
	};
	

	if (target_alloc_working_area(target, sizeof(rs14100_flash_erase_code),
			&erase_algorithm) != ERROR_OK) {
		LOG_WARNING("no working area available, can't do block memory writes");
		return ERROR_TARGET_RESOURCE_NOT_AVAILABLE;
	}

	retval = target_write_buffer(target, erase_algorithm->address,
			sizeof(rs14100_flash_erase_code),
			rs14100_flash_erase_code);
	if (retval != ERROR_OK){
		target_free_working_area(target, erase_algorithm);
		return retval;
	}

	init_reg_param(&reg_params[0], "r0", 32, PARAM_OUT);		/* first */
	init_reg_param(&reg_params[1], "r1", 32, PARAM_OUT);		/* last */

	buf_set_u32(reg_params[0].value, 0, 32, first);
	buf_set_u32(reg_params[1].value, 0, 32, last+1);

	armv7m_info.common_magic = ARMV7M_COMMON_MAGIC;
	armv7m_info.core_mode = ARM_MODE_THREAD;

	/* Run the algorithm */
	LOG_DEBUG("Running flash erase algorithm");
	retval = target_run_algorithm(target, 0 , NULL, 2 , reg_params,
		erase_algorithm->address,
		erase_algorithm->address + sizeof(rs14100_flash_erase_code) - 2,
		1000, &armv7m_info);

	if (retval != ERROR_OK)
		LOG_ERROR("Error executing flash init algorithm");
	else
	{
		for (int i = first; i <= last; i++)
			bank->sectors[i].is_erased = 1;
	}

	target_free_working_area(target, erase_algorithm);

	destroy_reg_param(&reg_params[0]);
	destroy_reg_param(&reg_params[1]);

	return retval;
}

static int rs14100_probe(struct flash_bank *bank)
{
	struct rs14100_flash_bank *rs14100_info = bank->driver_priv;
	int i;
	uint16_t num_sectors = 256; //for 1MB Flash, for 4MB Flash, value = 1024
	int sector_size = SECTOR_SIZE;
	uint32_t base_address = 0x8012000;
  	int retval = ERROR_OK;

	rs14100_info->probed = 0;

	if (bank->sectors)
	{
		free(bank->sectors);
		bank->sectors = NULL;
	}

	bank->base = base_address;
	bank->size = (num_sectors * sector_size) - 0x12000; //1MB - 72k (used for MBR)
	bank->num_sectors = num_sectors;
	bank->sectors = calloc(bank->num_sectors, sizeof(struct flash_sector));

	for (i = 0; i < bank->num_sectors; i++) 
	{
		bank->sectors[i].size = sector_size;
		bank->sectors[i].offset = i * sector_size;
		bank->sectors[i].is_erased = 0;
		bank->sectors[i].is_protected = 0;
	}

	// Here the probed flag is set in the bank private data.
	// This saves having to do the probe all the time since
	// other functions rely on this data existing.

  	retval = rs14100_init(bank);

	if (retval != ERROR_OK)
	  return retval;
	
	rs14100_info->probed = 1;

	return ERROR_OK;
}	


static int rs14100_auto_probe(struct flash_bank *bank)
{
	struct rs14100_flash_bank *rs14100_info = bank->driver_priv;
	if (rs14100_info->probed)
		return ERROR_OK;

	return rs14100_probe(bank);
}


static int get_rs14100_info(struct flash_bank *bank, char *buf, int buf_size)
{
	struct rs14100_flash_bank *rs14100_info = bank->driver_priv;
	
	LOG_ERROR("In Auto-Probe...");

	if (!(rs14100_info->probed)) {
		snprintf(buf, buf_size,
			"\nSPIFI flash bank not probed yet\n");
		return ERROR_OK;
	}

	return ERROR_OK;
}

uint32_t checksum_addition(const uint8_t *buf, uint32_t size, uint32_t prev_sum)
{
	uint32_t sum = prev_sum;
	uint32_t cnt;
	uint32_t cnt_limit;
	uint32_t dword;

	if (size == 0)
	{
 		return sum;
	}

	cnt_limit = (size & (~0x3));
	/* Accumulate checksum */
	for (cnt = 0; cnt < cnt_limit; cnt += 4)
	{
		dword = *(uint32_t *) &buf[cnt];
		sum += dword;
		if(sum < dword)
		{
			/* In addition operation, if result is lesser than any one of the operand
			 * it means carry is generated. 
			 * Incrementing the sum to get ones compliment addition */

			sum++;
		}
	}
	
	/* Handle non dword-sized case */
  	if(size & 0x3) {
		dword = 0xffffffff;
		dword = ~(dword << (8 * (size & 0x3)));
		/* Keeping only valid bytes and making upper bytes zeroes. */
		dword = (*(uint32_t *) &buf[cnt]) & dword;
		sum += dword;
		if(sum < dword)
		{
			sum++;
		}
	}

	return ~sum;
}

static int rs14100_write(struct flash_bank *bank, const uint8_t *buffer, uint32_t offset, uint32_t count)
{
	struct target *target = bank->target;
	uint32_t buffer_size = 16384;
	struct working_area *write_algorithm;
	struct working_area *source;
	struct reg_param reg_params[6];
	struct armv7m_algorithm armv7m_info;
	int retval = ERROR_OK;
	uint32_t check_sum = 0;

	if (target->state != TARGET_HALTED) {
		LOG_ERROR("Target not halted");
		return ERROR_TARGET_NOT_HALTED;
	}


	if (offset + count > bank->size) {
		LOG_WARNING("Writes past end of flash. Extra data discarded.");
		count = bank->size - offset;
	}

	if(offset == 0x00)
	{
		check_sum  = checksum_addition(buffer , 236 , 1);
	}

	/* see contrib/loaders/flash/rs14100/rs14100_write.s for src */
	static const uint8_t rs14100_flash_write_code[] = {
		0x4f, 0xf4, 0x40, 0x66, 0xc0, 0xf2, 0x01, 0x06,
  		0xc4, 0xf8, 0x04, 0x60, 0x00, 0xf0, 0x89, 0xf8,
  		0x4f, 0xf0, 0x01, 0x06, 0xc0, 0xf2, 0x08, 0x26,
  		0xc4, 0xf8, 0x10, 0x60, 0x4f, 0xf4, 0x40, 0x66,
  		0xc0, 0xf2, 0x01, 0x06, 0xc4, 0xf8, 0x04, 0x60,
  		0x42, 0xf2, 0xec, 0x09, 0xc0, 0xf2, 0x01, 0x09,
  		0xd0, 0xf8, 0x00, 0x80, 0xb8, 0xf1, 0x00, 0x0f,
  		0x00, 0xf0, 0x94, 0x80, 0x47, 0x68, 0x47, 0x45,
  		0xf6, 0xd0, 0x4f, 0xf0, 0x08, 0x06, 0xc4, 0xf8,
  		0x80, 0x60, 0x4f, 0xf0, 0x06, 0x06, 0xc4, 0xf8,
  		0x40, 0x60, 0x4f, 0xf0, 0x02, 0x06, 0xc0, 0xf2,
  		0x08, 0x26, 0xc4, 0xf8, 0x10, 0x60, 0x00, 0xf0,
  		0x56, 0xf8, 0x4f, 0xf0, 0x01, 0x06, 0xc0, 0xf2,
  		0x08, 0x26, 0xc4, 0xf8, 0x10, 0x60, 0x4f, 0xf0,
  		0x08, 0x06, 0xc4, 0xf8, 0x80, 0x60, 0x4f, 0xf0,
  		0x02, 0x06, 0xc4, 0xf8, 0x40, 0x60, 0x4f, 0xf0,
  		0x02, 0x06, 0xc0, 0xf2, 0x08, 0x26, 0xc4, 0xf8,
  		0x10, 0x60, 0x00, 0xf0, 0x40, 0xf8, 0x4f, 0xf0,
  		0x18, 0x06, 0xc4, 0xf8, 0x80, 0x60, 0x16, 0x46,
  		0x02, 0xf1, 0x04, 0x02, 0xc4, 0xf8, 0x40, 0x60,
  		0x4f, 0xf0, 0x02, 0x06, 0xc0, 0xf2, 0x08, 0x26,
  		0xc4, 0xf8, 0x10, 0x60, 0x00, 0xf0, 0x2f, 0xf8,
  		0x4f, 0xf0, 0xf1, 0x06, 0xc4, 0xf8, 0x14, 0x60,
  		0x4f, 0xf0, 0x20, 0x06, 0xc4, 0xf8, 0x80, 0x60,
  		0x57, 0xf8, 0x04, 0x6b, 0xa2, 0xf1, 0x04, 0x08,
  		0xc8, 0x45, 0x08, 0xbf, 0x2e, 0x46, 0x26, 0x64,
  		0x4f, 0xf0, 0x02, 0x06, 0xc0, 0xf2, 0x08, 0x26,
  		0xc4, 0xf8, 0x10, 0x60, 0x00, 0xf0, 0x17, 0xf8,
  		0x4f, 0xf0, 0xf0, 0x06, 0xc4, 0xf8, 0x14, 0x60,
  		0x4f, 0xf0, 0x01, 0x06, 0xc0, 0xf2, 0x08, 0x26,
  		0xc4, 0xf8, 0x10, 0x60, 0x00, 0xf0, 0x0b, 0xf8,
  		0x00, 0xf0, 0x0e, 0xf8, 0x8f, 0x42, 0x28, 0xbf,
  		0x00, 0xf1, 0x08, 0x07, 0xc0, 0xf8, 0x04, 0x70,
  		0x04, 0x3b, 0x7b, 0xb1, 0x90, 0xe7, 0x26, 0x6a,
  		0x16, 0xf0, 0x01, 0x0f, 0xfb, 0xd1, 0x70, 0x47,
  		0x4f, 0xf4, 0x80, 0x56, 0x01, 0x3e, 0xfd, 0xd1,
  		0x70, 0x47, 0x26, 0x6a, 0x16, 0xf4, 0x80, 0x5f,
  		0xfb, 0xd1, 0x70, 0x47, 0x4f, 0xf0, 0x08, 0x06,
  		0xc4, 0xf8, 0x80, 0x60, 0x4f, 0xf0, 0x04, 0x06,
  		0xc4, 0xf8, 0x40, 0x60, 0x4f, 0xf0, 0x02, 0x06,
  		0xc0, 0xf2, 0x08, 0x26, 0xc4, 0xf8, 0x10, 0x60,
  		0xff, 0xf7, 0xe1, 0xff, 0x4f, 0xf0, 0x01, 0x06,
  		0xc0, 0xf2, 0x08, 0x26, 0xc4, 0xf8, 0x10, 0x60,
  		0x4f, 0xf4, 0x44, 0x66, 0xc0, 0xf2, 0x01, 0x06,
  		0xc4, 0xf8, 0x04, 0x60, 0x00, 0xbe,
	};
	

	if (target_alloc_working_area(target, sizeof(rs14100_flash_write_code),
			&write_algorithm) != ERROR_OK) {
		LOG_WARNING("no working area available, can't do block memory writes");
		return ERROR_TARGET_RESOURCE_NOT_AVAILABLE;
	}

	retval = target_write_buffer(target, write_algorithm->address,
			sizeof(rs14100_flash_write_code),
			rs14100_flash_write_code);
	if (retval != ERROR_OK){
		target_free_working_area(target, write_algorithm);
		return retval;
	}

	/* memory buffer */
	while (target_alloc_working_area_try(target, buffer_size, &source) != ERROR_OK) {
		buffer_size /= 2;
		if (buffer_size <= 256) {
			/* we already allocated the writing code, but failed to get a
			 * buffer, free the algorithm */
			target_free_working_area(target, write_algorithm);

			LOG_WARNING("no large enough working area available, can't do block memory writes");
			return ERROR_TARGET_RESOURCE_NOT_AVAILABLE;
		}
	}


	armv7m_info.common_magic = ARMV7M_COMMON_MAGIC;
	armv7m_info.core_mode = ARM_MODE_THREAD;

	init_reg_param(&reg_params[0], "r0", 32, PARAM_IN_OUT);		/* buffer start, status (out) */
	init_reg_param(&reg_params[1], "r1", 32, PARAM_OUT);		/* buffer end */
	init_reg_param(&reg_params[2], "r2", 32, PARAM_OUT);		/* target address */
	init_reg_param(&reg_params[3], "r3", 32, PARAM_OUT);		/* count (in bytes) */
	init_reg_param(&reg_params[4], "r4", 32, PARAM_OUT);		/* flash base */
	init_reg_param(&reg_params[5], "r5", 32, PARAM_OUT);		/* checksum */

	buf_set_u32(reg_params[0].value, 0, 32, source->address);
	buf_set_u32(reg_params[1].value, 0, 32, source->address + source->size);
	buf_set_u32(reg_params[2].value, 0, 32, flash_offset);
	buf_set_u32(reg_params[3].value, 0, 32, count);
	buf_set_u32(reg_params[4].value, 0, 32, QSPI_BASE);
	buf_set_u32(reg_params[5].value, 0, 32, check_sum);

	retval = target_run_flash_async_algorithm(target, buffer, count, 1,
			0, NULL,
			6, reg_params,
			source->address, source->size,
			write_algorithm->address, 0,
			&armv7m_info);

	if (retval != ERROR_OK)	
		LOG_ERROR("Error executing flash write algorithm");


	target_free_working_area(target, source);
	target_free_working_area(target, write_algorithm);

	destroy_reg_param(&reg_params[0]);
	destroy_reg_param(&reg_params[1]);
	destroy_reg_param(&reg_params[2]);
	destroy_reg_param(&reg_params[3]);
	destroy_reg_param(&reg_params[4]);
	destroy_reg_param(&reg_params[5]);

	return retval;
}


static int rs14100_protect_check(struct flash_bank *bank)
{
  return ERROR_OK;
}

static int rs14100_protect(struct flash_bank *bank, int set, int first, int last)
{
  return ERROR_OK;
}

static int rs14100_read(struct flash_bank *bank, uint8_t *buffer, uint32_t offset, uint32_t count)
{
  return ERROR_OK;
}

static int rs14100_erase_check(struct flash_bank *bank)
{
  return ERROR_OK;
}

struct flash_driver rs14100_flash = {
	.name = "rs14100",
	.flash_bank_command = rs14100_flash_bank_command,
	.erase = rs14100_erase,
	.protect = rs14100_protect,
	.write = rs14100_write,
	.read = rs14100_read,
	.probe = rs14100_probe,
	.auto_probe = rs14100_auto_probe,
	.erase_check = rs14100_erase_check,
	.protect_check = rs14100_protect_check,
	.info = get_rs14100_info,
};

