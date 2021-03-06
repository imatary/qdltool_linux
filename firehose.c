#include "firehose.h"

char respbuf[MAX_RESP_LENGTH] = {0};
char rubbish[MAX_LENGTH] = {0};
char bigchunk[MAX_LENGTH*200] = {0};
size_t NUM_DISK_SECTORS = 0;

char *respbuf_ref(size_t *len)
{
    if(len)
        *len = sizeof(respbuf);
    memset(respbuf, 0, sizeof(respbuf));
    return respbuf;
}

static response_t _response(parse_xml_reader_func func)
{
    int r, status;
    int retry = MAX_RETRY;
    size_t len = 0;  
    char *buf = respbuf_ref(&len);
    char *ptr = buf;
    response_t response;

    while(retry>0){
        r = 0;
        status = read_response(ptr, len-(ptr-buf), &r);
        if (r > 0){ 
            ptr += r;
        }
        if (status < 0){
            if(ptr>buf){
                xml_reader_t reader;
                xmlInitReader(&reader, buf, ptr-buf);
                response = func(&reader);
                if(response != NIL){
                    return response;
                }
                retry --; 
                usleep(1000);
                continue;
            }
        }
        retry --;
        usleep(1000);
    }
    return NIL;
}

response_t get_maxpayload_support_from_xreader(xml_reader_t *reader, char *maxpayload_support, int sz)
{
    char ack[128] = {0};
    bool is_response_tag = FALSE;
    xml_token_t token;
    while ((token = xmlGetToken(reader)) != XML_TOKEN_NONE) {
        if(token == XML_TOKEN_TAG){
            bzero(ack, sizeof(ack));
            bzero(maxpayload_support, sz);
            is_response_tag = xmlIsTag(reader, "response");
            if(is_response_tag){
                bzero(ack, sizeof(ack));
            }
        }

        if (token == XML_TOKEN_ATTRIBUTE && is_response_tag) {
            if (xmlIsAttribute(reader, "value"))
                xmlGetAttributeValue(reader, ack, sizeof(ack));

            if (xmlIsAttribute(reader, "MaxPayloadSizeToTargetInBytesSupported"))
                xmlGetAttributeValue(reader, maxpayload_support, sz);

            if (ack[0] && maxpayload_support[0]){
                return  strncasecmp(ack, "ACK", 3)==0?ACK:NAK;
            }
        }
    }
    return NIL;
}

response_t firehose_get_maxpayload_support_response(get_maxpayload_support_from_xreader_func func,
                                                    char *maxpayload_support, int sz)
{
    int r, status;
    size_t len;
    int retry = MAX_RETRY;
    char *buf = respbuf_ref(&len);
    char *ptr = buf;
    response_t response;

    while(retry>0){
        r = 0;
        status = read_response(ptr, len-(ptr-buf), &r);
        if (r > 0){
            ptr += r;
        }
        if (status < 0){
            if(ptr>buf){
                xml_reader_t reader;
                xmlInitReader(&reader, buf, ptr-buf);
                response = func(&reader, maxpayload_support, sz);
                if(response != NIL){
                    return response;
                }
                retry --; 
                usleep(1000);
                continue;
            }
        }
        retry --; 
        usleep(1000);
    }
    return NIL;
}

response_t firehose_get_maxpayload_support(char *maxpayload_support, int sz)
{
    return firehose_get_maxpayload_support_response(get_maxpayload_support_from_xreader, maxpayload_support, sz);
}

response_t common_response_xml_reader(xml_reader_t *reader)
{
    xml_token_t token;
    char value[128] = {0};
    int is_response_tag = 0;
    while ((token = xmlGetToken(reader)) != XML_TOKEN_NONE) {
        if(token == XML_TOKEN_TAG)
            is_response_tag = xmlIsTag(reader, "response");
        if (token == XML_TOKEN_ATTRIBUTE && is_response_tag){
            if (xmlIsAttribute(reader, "Value")){
                xmlGetAttributeValue(reader, value, sizeof(value));
                return (strncasecmp("ACK", value, 3)==0)?ACK:NAK;
            }   
        }   
    }   
    return NIL;
}

response_t common_response()
{
    return _response(common_response_xml_reader);
}

response_t firehose_configure_response()
{
    return common_response();
}

int send_firehose_configure(firehose_configure_t c)
{
    return send_command(c.xml, strlen(c.xml));
}

response_t process_firehose_configure()
{
    response_t resp;
    char *format = XML_HEADER
                   "<data><configure "
                   "MaxPayloadSizeToTargetInBytes=\"%s\" "
                   "verbose=\""FIREHOSE_VERBOSE"\" "
                   "ZlpAwareHost=\"0\" "
                   "/></data>";

    char maxpayload[10] = "4096";
    char maxpayload_support[10] = {0};
    firehose_configure_t configure;

    sprintf(configure.xml, format, maxpayload);
    send_firehose_configure(configure);
    resp = firehose_get_maxpayload_support(maxpayload_support, sizeof(maxpayload_support));
    if(resp != ACK)
        return resp;
    memset(&configure, 0, sizeof(configure));
    sprintf(configure.xml, format, maxpayload_support);
    send_firehose_configure(configure);
    return firehose_configure_response();
}


response_t firehose_emmc_info_response_xml_reader(xml_reader_t *reader)
{
    xml_token_t token;
    char ack[128] = {0};
    char value[128] = {0};
    int is_response_tag = 0;
    int is_emmc_size_log_tag = 0;
    while ((token = xmlGetToken(reader)) != XML_TOKEN_NONE) {
        if(token == XML_TOKEN_TAG){
            is_emmc_size_log_tag = xmlIsTag(reader, "log");
            if (is_emmc_size_log_tag)
                is_response_tag = 0;
            else{
                is_response_tag = xmlIsTag(reader, "response");
                if (is_response_tag)
                    is_emmc_size_log_tag = 0;
            }
        }
        if (token == XML_TOKEN_ATTRIBUTE && is_response_tag){
            if (xmlIsAttribute(reader, "Value")){
                xmlGetAttributeValue(reader, ack, sizeof(ack));
            }
        }
        if (token == XML_TOKEN_ATTRIBUTE && is_emmc_size_log_tag){
            if (xmlIsAttribute(reader, "Value")){
                xmlGetAttributeValue(reader, value, sizeof(value));
                if (strcasestr(value, "eMMC size=")){
                    sscanf(value, "eMMC size=%zu", &NUM_DISK_SECTORS);
                }
            }
        }

        if(ack[0]){
            if (strncasecmp("ACK", ack, 3)==0 && NUM_DISK_SECTORS> 0)
                return ACK;
            if (strncasecmp("ACK", ack, 3)){
                return NAK;
            }
        }
    }
    return NIL;
}

response_t firehose_emmc_info_reponse()
{
    return _response(firehose_emmc_info_response_xml_reader);
}

response_t firehose_emmc_info()
{
    char *cmd = "<?xml version=\"1.0\" ?><data><eMMCinfo /></data>";
    send_command(cmd, strlen(cmd));
    return firehose_emmc_info_reponse();
}


int init_firehose_erase(xml_reader_t *reader, firehose_erase_t *e)
{
    xml_token_t token;
    while ((token = xmlGetToken(reader)) != XML_TOKEN_NONE) {
        if (token == XML_TOKEN_ATTRIBUTE) {
            if (xmlIsAttribute(reader, "num_partition_sectors")){
                xmlGetAttributeValue(reader, e->sector_numbers, 128);
            }
            if (xmlIsAttribute(reader, "start_sector")){
                xmlGetAttributeValue(reader, e->start_sector, 128);
            }
            if (xmlIsAttribute(reader, "label")){
                xmlGetAttributeValue(reader, e->label, 128);
            }
            if (xmlIsAttribute(reader, "physical_partition_number")) {
                //this should be "storagedrive", it may change in the future from Qualcommn
                xmlGetAttributeValue(reader, e->storagedrive, 128); 
            }
        }
    }
   
    char *format= XML_HEADER
                  "<data><erase "
                  "start_sector=\"%s\" "
                  "num_partition_sectors=\"%s\" "
                  "storagedrive=\"%s\" "
                  "/></data>";
    
    bzero(e->xml, sizeof(e->xml));
    sprintf(e->xml, format, e->start_sector, e->sector_numbers, e->storagedrive);
}

int send_firehose_erase(firehose_erase_t erase)
{
    return send_command(erase.xml, strlen(erase.xml));
}

response_t firehose_erase_response()
{
    return common_response();
}

response_t process_firehose_erase_xml(char *xml, int length)
{
    response_t resp;
    firehose_erase_t erase;
    memset(&erase, 0, sizeof(firehose_erase_t));
    xml_reader_t reader;
    xmlInitReader(&reader, xml, length);
    init_firehose_erase(&reader, &erase);
    send_firehose_erase(erase);
    resp = firehose_erase_response();
    if (resp == ACK){
        info("formatting %s succeed", erase.label);
    } else
    if (resp == NAK){
        info("formatting %s failed", erase.label);
    } else
    if (resp == NIL){
        info("formatting %s nil", erase.label);
    }
    return resp;
}


response_t firehose_patch_response()
{
    return common_response();
}

void init_firehose_patch(xml_reader_t *reader, firehose_patch_t *patch)
{
    bzero(patch->xml, sizeof(patch->xml));
    char *template = XML_HEADER
                     "<data>"
                     "%s"
                     "</data>";
    sprintf(patch->xml, template, reader->buffer);

    xml_token_t token;
    while ((token = xmlGetToken(reader)) != XML_TOKEN_NONE) {
        if (token == XML_TOKEN_ATTRIBUTE) {
            if (xmlIsAttribute(reader, "filename")){
                xmlGetAttributeValue(reader, patch->filename, sizeof(patch->filename));
            }

            if (xmlIsAttribute(reader, "what")){
                xmlGetAttributeValue(reader, patch->what, sizeof(patch->what));
            }
        }
    }
}

int send_firehose_patch(firehose_patch_t patch)
{
    return send_command(patch.xml, strlen(patch.xml));
}

response_t process_firehose_patch_xml(char *xml, int length)
{
    response_t resp;
    firehose_patch_t patch;
    memset(&patch, 0, sizeof(firehose_patch_t));
    xml_reader_t reader;
    xmlInitReader(&reader, xml, length);
    init_firehose_patch(&reader, &patch);
    send_firehose_patch(patch);
    resp = firehose_patch_response();
    if (resp == ACK){
        info("%s succeed", patch.what);
    } else
    if (resp == NAK){
        info("%s failed", patch.what);
    } else
    if (resp == NIL){
        info("%s nil", patch.what);
    }
    return resp; 
}


response_t power_response()
{
    return common_response();
}

void init_firehose_power(char *act, firehose_power_t *power)
{
    memcpy(power->act, act, sizeof(power->act));
    char *format = XML_HEADER
                   "<data><power value=\"%s\" "
                   "delayinseconds=\"3\" /></data>";
    sprintf(power->xml, format, act);
    send_command(power->xml, strlen(power->xml));
}

int send_firehose_power(firehose_power_t power)
{
    return send_command(power.xml, strlen(power.xml));
}

response_t process_power_action(char *act)
{
    firehose_power_t power;
    memset(&power, 0, sizeof(firehose_power_t));
    init_firehose_power(act, &power);
    send_firehose_power(power);  
    return  power_response();
}


void update_xml_of_firehose_progarm(firehose_program_t *p)
{
    memset(p->xml, 0, sizeof(p->xml));
    char *template= XML_HEADER
                    "<data> <program "
                    "SECTOR_SIZE_IN_BYTES=\"%zu\" "
                    "num_partition_sectors=\"%zu\" "
                    "physical_partition_number=\"%zu\" "
                    "start_sector=\"%zu\" "
                    "/></data>";
    sprintf(p->xml, template, p->sector_size, p->sector_numbers,
                p->physical_partition_number, p->start_sector);
}

void update_xml_of_firehose_simlock(firehose_simlock_t *slk)
{
    memset(slk->xml, 0, sizeof(slk->xml));
    char *template= XML_HEADER
                    "<data> <simlock "
                    "SECTOR_SIZE_IN_BYTES=\"%zu\" "
                    "num_partition_sectors=\"%zu\" "
                    "physical_partition_number=\"%zu\" "
                    "start_sector=\"%zu\" "
                    "len=\"%zu\" "
                    "/></data>";

    sprintf(slk->xml, template, slk->sector_size, slk->sector_numbers,
                slk->physical_partition_number, slk->start_sector, slk->len);
}

int init_firehose_program_from_xml_reader(xml_reader_t *reader, firehose_program_t *program)
{
    xml_token_t token;
    while ((token = xmlGetToken(reader)) != XML_TOKEN_NONE) {
        char tempbuf[256] = {0};
        if (token == XML_TOKEN_ATTRIBUTE) {
            if (xmlIsAttribute(reader, "SECTOR_SIZE_IN_BYTES")){
                xmlGetAttributeValue(reader, tempbuf, sizeof(tempbuf));
                program->sector_size = firehose_strtoint(tempbuf);
                continue;
            }
            if (xmlIsAttribute(reader, "file_sector_offset")){
                xmlGetAttributeValue(reader, tempbuf, sizeof(tempbuf));
                program->file_sector_offset = firehose_strtoint(tempbuf);
                continue;
            }
            if (xmlIsAttribute(reader, "num_partition_sectors")){
                xmlGetAttributeValue(reader, tempbuf, sizeof(tempbuf));
                program->sector_numbers = firehose_strtoint(tempbuf);
                continue;
            }
            if (xmlIsAttribute(reader, "physical_partition_number")){
                xmlGetAttributeValue(reader, tempbuf, sizeof(tempbuf));
                program->physical_partition_number = firehose_strtoint(tempbuf);
                continue;
            }
            if (xmlIsAttribute(reader, "start_sector")){
                xmlGetAttributeValue(reader, tempbuf, sizeof(tempbuf));
                program->start_sector = firehose_strtoint(tempbuf);
                continue;
            }
            if (xmlIsAttribute(reader, "filename")){
                xmlGetAttributeValue(reader, program->filename, sizeof(program->filename));
                continue;
            }
            if (xmlIsAttribute(reader, "label")){
                xmlGetAttributeValue(reader, program->label, sizeof(program->label));
                continue;
            }
            if (xmlIsAttribute(reader, "sparse")){
                xmlGetAttributeValue(reader, tempbuf, sizeof(tempbuf));
                if(!strcasecmp(tempbuf, "true"))
                    program->sparse = True;
                if(!strcasecmp(tempbuf, "false"))
                    program->sparse = False;
                continue;
            }
        }
    }

    update_xml_of_firehose_progarm(program);
}

int send_program(firehose_program_t p)
{
    clear_rubbish();
    return send_command(p.xml, strlen(p.xml));
}

response_t program_response_xml_reader(xml_reader_t *reader)
{
    xml_token_t token;
    char ack[128] = {0};
    char rawmode[128] = {0};
    bool is_response_tag = FALSE;
    while ((token = xmlGetToken(reader)) != XML_TOKEN_NONE) {
        if(token == XML_TOKEN_TAG){
            is_response_tag = xmlIsTag(reader, "response");
            if(is_response_tag){
                bzero(ack, sizeof(ack));
                bzero(rawmode, sizeof(rawmode));
            }
        }
        if (token == XML_TOKEN_ATTRIBUTE && is_response_tag) {
            if (xmlIsAttribute(reader, "value"))
                xmlGetAttributeValue(reader, ack, sizeof(ack));
            if (xmlIsAttribute(reader, "rawmode"))
                xmlGetAttributeValue(reader, rawmode, sizeof(rawmode));
            if (ack[0] && rawmode[0]){
                return (strncasecmp(ack, "ACK", 3)==0
                        && strncasecmp(rawmode, "true", 4)==0)?ACK:NAK;
            }
        }
    }
    return NIL;
}

response_t program_response()
{
    return  _response(program_response_xml_reader);
}

response_t transmit_chunk_response_xml_reader(xml_reader_t *reader)
{
    xml_token_t token;
    char ack[128] = {0};
    char rawmode[128] = {0};
    bool is_response_tag = FALSE;
    while ((token = xmlGetToken(reader)) != XML_TOKEN_NONE) {
        if(token == XML_TOKEN_TAG){
            is_response_tag = xmlIsTag(reader, "response");
            if(is_response_tag){
                bzero(ack, sizeof(ack));
                bzero(rawmode, sizeof(rawmode));
            }
        }
        if (token == XML_TOKEN_ATTRIBUTE && is_response_tag) {
            if (xmlIsAttribute(reader, "value"))
                xmlGetAttributeValue(reader, ack, sizeof(ack));
            if (xmlIsAttribute(reader, "rawmode"))
                xmlGetAttributeValue(reader, rawmode, sizeof(rawmode));
            if (ack[0] && rawmode[0]){
                return (strncasecmp(ack, "ACK", 3)==0
                        && strncasecmp(rawmode, "false", 5)==0)?ACK:NAK;
            }
        }
    }
    return NIL;
}

response_t transmit_chunk_response()
{
    return _response(transmit_chunk_response_xml_reader);
}

response_t transmit_chunk(char *chunk, firehose_program_t p)
{
    int w=0, status;
    int payload = 16*1024; //16K
    size_t total_size = p.sector_size*p.sector_numbers;
    char *ptr = chunk;
    char *end = chunk + total_size;
    size_t to_send = 0;
    response_t response;
    update_xml_of_firehose_progarm(&p); //actually, this shall have been done
    send_program(p);
    response = program_response();
    if (response == NAK)
        xerror("NAK program response");
    if (response == NIL)
        xerror("no ACK or NAK found in response");
    clear_rubbish();
    while(ptr < end){
        to_send = min(end-ptr, payload);
        status = send_data(ptr, to_send, &w);
        if ((status < 0) || (w != to_send)){
            xerror("failed, status: %d  w: %d", status, w);
        }
        ptr += w;
        printf("\r %zu / %zu    ", ptr-chunk, total_size); fflush (stdout);
    }
    memset(chunk, 0, total_size);
    response = transmit_chunk_response();
    if (response == ACK)
        info("  succeeded");
    else
        info("  failed");
    return response;
}


int clear_rubbish()
{
    bzero(rubbish, sizeof(rubbish));
    return read_rx_timeout(rubbish, sizeof(rubbish), NULL, 10);
}

int read_response(void *buf, int length, int *act)
{
    memset(buf, 0, length);
    return read_rx_timeout(buf, length, act, 100);
}

int send_command(void *buf, int len)
{
    clear_rubbish();
    return write_tx(buf, len, NULL);
}


int init_firehose_simlock_from_xml_reader(xml_reader_t *reader, firehose_simlock_t *slk)
{
    xml_token_t token;
    while ((token = xmlGetToken(reader)) != XML_TOKEN_NONE) {
        char tempbuf[256] = {0};
        if (token == XML_TOKEN_ATTRIBUTE) {
            if (xmlIsAttribute(reader, "SECTOR_SIZE_IN_BYTES")){
                xmlGetAttributeValue(reader, tempbuf, sizeof(tempbuf));
                slk->sector_size = firehose_strtoint(tempbuf);
                continue;
            }
            if (xmlIsAttribute(reader, "file_sector_offset")){
                xmlGetAttributeValue(reader, tempbuf, sizeof(tempbuf));
                slk->file_sector_offset = firehose_strtoint(tempbuf);
                continue;
            }
            if (xmlIsAttribute(reader, "num_partition_sectors")){
                xmlGetAttributeValue(reader, tempbuf, sizeof(tempbuf));
                slk->sector_numbers = firehose_strtoint(tempbuf);
                continue;
            }
            if (xmlIsAttribute(reader, "physical_partition_number")){
                xmlGetAttributeValue(reader, tempbuf, sizeof(tempbuf));
                slk->physical_partition_number = firehose_strtoint(tempbuf);
                continue;
            }
            if (xmlIsAttribute(reader, "start_sector")){
                xmlGetAttributeValue(reader, tempbuf, sizeof(tempbuf));
                slk->start_sector = firehose_strtoint(tempbuf);
                continue;
            }
            if (xmlIsAttribute(reader, "filename")){
                xmlGetAttributeValue(reader, slk->filename, sizeof(slk->filename));
                continue;
            }
            if (xmlIsAttribute(reader, "label")){
                xmlGetAttributeValue(reader, slk->label, sizeof(slk->label));
                continue;
            }
            if (xmlIsAttribute(reader, "sparse")){
                xmlGetAttributeValue(reader, tempbuf, sizeof(tempbuf));
                if(!strcasecmp(tempbuf, "true"))
                    slk->sparse = True;
                if(!strcasecmp(tempbuf, "false"))
                    slk->sparse = False;
                continue;
            }
        }
    }
    update_xml_of_firehose_simlock(slk);
}

int send_simlock(firehose_simlock_t slk)
{
    clear_rubbish();
    return send_command(slk.xml, strlen(slk.xml));
}

response_t transmit_chunk_simlock(char *chunk, firehose_simlock_t slk)
{
    int w=0, status;
    int payload = 16*1024; //16k 
    size_t total_size = slk.len;
    char *ptr = chunk;
    char *end = chunk + total_size;
    size_t to_send = 0;
    response_t response;
    send_simlock(slk);
    response = program_response(); //just using program_response here, it's the same
    if (response == NAK)
        xerror("NAK simlock response");
    if (response == NIL)
        xerror("no ACK or NAK found in simlock response");
    clear_rubbish();
    while(ptr < end){
        to_send = min(end-ptr, payload);
        status = send_data(ptr, to_send, &w);
        if ((status < 0) || (w != to_send)){
            xerror("failed, status: %d  w: %d", status, w);
        }
        ptr += w;
        printf("\r %zu / %zu    ", ptr-chunk, total_size); fflush (stdout);
    }
    memset(chunk, 0, total_size);
    response = transmit_chunk_response();
    if (response == ACK)
        info("  succeeded");
    else
        info("  failed");
    return response;
}
