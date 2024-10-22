// SIPTestServer.cpp : This file contains the 'main' function. Program execution begins and ends there.
//
//#include <pjsua2.hpp>
#include <map>
#include <string>

#include <iostream>
#include <pjlib.h>
#include <pjlib-util.h>
#include <pjnath.h>
#include <pjsip.h>
#include <pjsip_ua.h>
#include <pjsip_simple.h>
#include <pjsua-lib/pjsua.h>
#include <pjmedia.h>
#include <pjmedia-codec.h>

struct Session {
    pjsua_call_id caller_call_id;  // Call ID for the caller
    pjsua_call_id callee_call_id;  // Call ID for the callee
};

std::map<std::string, Session*> sessions;

// globals
pjsua_transport_id transport_id;
std::map<std::string, std::string> registrations; // Key: username, Value: contact URI

// function to convert pj_str_t to std::string
std::string buffer_to_std_string(const char* buf , pj_size_t size)
{
    return std::string(buf, size);
}

// method to handle registration requests
void handle_register(pjsip_rx_data* rdata)
{
    PJ_LOG(3, ("handle_register: ", "Handling REGISTER request"));

    // extract username from header
    pjsip_from_hdr* from_hdr = (pjsip_from_hdr*)pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_FROM, NULL);
    if (!from_hdr)
    {
        return;
    }
 
    char uri_buf[256];
    pj_size_t uri_buf_size = sizeof(uri_buf);
    pj_size_t len = from_hdr->uri->vptr->p_print(PJSIP_URI_IN_FROMTO_HDR , from_hdr->uri , uri_buf , uri_buf_size);
    if (len == 0)
    {
        return;
    }

    std::string username = buffer_to_std_string(uri_buf , len);

    pjsip_contact_hdr* contact_hdr = (pjsip_contact_hdr*)pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_CONTACT, NULL);
    if (!contact_hdr)
    {
        return;
    }

    len = contact_hdr->uri->vptr->p_print(PJSIP_URI_IN_CONTACT_HDR, contact_hdr->uri, uri_buf, uri_buf_size);
    if (len == 0)
    {
        return;
    }

    std::string contact_uri = buffer_to_std_string(uri_buf, len);

    // add this registration into our map
    registrations[username] = contact_uri;

    PJ_LOG(3, ("hangle_register: ", "Registered User - %s Contact - %s", username.c_str(), contact_uri.c_str()));


    // send response message
    pjsip_tx_data* tdata;
    pj_status_t status;

    // Create 200 OK response
    status = pjsip_endpt_create_response(pjsua_get_pjsip_endpt(), rdata, 200, NULL, &tdata);
    if (status == PJ_SUCCESS) {
        // Send the response
        status = pjsip_endpt_send_response2(pjsua_get_pjsip_endpt(), rdata, tdata, NULL, NULL);
        if (status != PJ_SUCCESS) {
            //PJ_LOG(3, (__FILE__), ("Failed to send 200 OK response"));
        }
        else {
            //PJ_LOG(3, (__FILE__), ("Sent 200 OK response to REGISTER request"));
        }
    }
    else {
        //PJ_LOG(3, (__FILE__), ("Failed to create 200 OK response"));
    }
}

void handle_invite(pjsip_rx_data* rdata)
{
    PJ_LOG(3, ("handle_invite: ", "Handling INVITE request..."));

    pj_status_t status;
    pjsip_tx_data* tdata;

    // Extract the To header to get the destination username
    pjsip_to_hdr* to_hdr = (pjsip_to_hdr*)pjsip_msg_find_hdr(rdata->msg_info.msg, PJSIP_H_TO, NULL);
    char buf[128];
    pjsip_uri* to_uri = (pjsip_uri*)pjsip_uri_get_uri(to_hdr->uri);
    pjsip_uri_print(PJSIP_URI_IN_FROMTO_HDR, to_uri, buf, sizeof(buf));
    std::string dest_username(buf);

    // Find the destination contact URI
    auto it = registrations.find(dest_username);
    if (it == registrations.end()) {
        PJ_LOG(3, ("handle_invite", "Destination user not found"));
        return;
    }
    std::string contact_uri_str = it->second;

    // Parse the contact URI
    pj_str_t pj_contact_uri = pj_str(const_cast<char*>(contact_uri_str.c_str()));
    pj_pool_t* pool = pjsip_endpt_create_pool(pjsua_get_pjsip_endpt(), "pool", 512, 512);
    pjsip_uri* new_contact_uri = pjsip_parse_uri(pool, pj_contact_uri.ptr, pj_contact_uri.slen, 0);
    pjsip_response_addr response_address;
    status = pjsip_get_response_addr(pool, rdata, &response_address);
    if (status != PJ_SUCCESS)
    {
        PJ_LOG(3, ("handle_invite", "Failed to get response address"));
        return;
    }
    // Answer the incoming INVITE with a 100 Trying response
    status = pjsip_endpt_create_response(pjsua_get_pjsip_endpt(), rdata, 100, NULL, &tdata);
    if (status == PJ_SUCCESS) {
        pjsip_endpt_send_response(pjsua_get_pjsip_endpt(), &response_address, tdata, NULL, NULL);
    }

    // Create a new INVITE session for the caller
    pjsua_call_id caller_call_id;
    //status = pjsua_call_answer(rdata->msg_info.cid->id, 180, NULL, NULL);
    if (status != PJ_SUCCESS) {
        PJ_LOG(3, ("handle_invite", "Failed to answer INVITE for caller"));
        pj_pool_release(pool);
        return;
    }

    // Create a new INVITE session for the callee
    pjsua_call_id callee_call_id;
    pjsua_call_setting call_setting;
    pjsua_call_setting_default(&call_setting);
    status = pjsua_call_make_call(0, &pj_contact_uri, &call_setting, NULL, NULL, &callee_call_id);
    if (status != PJ_SUCCESS) {
        PJ_LOG(3, ("handle_invite", "Failed to create INVITE session for callee"));
        //pjsua_call_hangup(caller_call_id, 0, NULL, NULL);
        pj_pool_release(pool);
        return;
    }

    // Store the session
    Session* session = new Session;
   // session->caller_call_id = caller_call_id;
    //session->callee_call_id = callee_call_id;
    sessions["test"] = session;

    pj_pool_release(pool);
}


// Registrar module's incoming request handler
static pj_bool_t on_rx_request(pjsip_rx_data* rdata) {
    PJ_LOG(3, ("on rx request: ", "Received SIP request"));

    if (rdata->msg_info.msg->line.req.method.id == PJSIP_REGISTER_METHOD) 
    {
        handle_register(rdata);
        return PJ_TRUE;
    }
    else if (rdata->msg_info.msg->line.req.method.id == PJSIP_INVITE_METHOD)
    {
        handle_invite(rdata);
        return PJ_TRUE;
    }
    return PJ_FALSE;
}

// Define the registrar module
static pjsip_module mod_simple_registrar = {
    NULL, NULL,                         /* prev, next.      */
    { const_cast<char*>("mod-simple-registrar"), 19 },     /* Name.            */
    -1,                                 /* Id               */
    PJSIP_MOD_PRIORITY_APPLICATION,     /* Priority         */
    NULL,                               /* load()           */
    NULL,                               /* start()          */
    NULL,                               /* stop()           */
    NULL,                               /* unload()         */
    &on_rx_request,                     /* on_rx_request()  */
    NULL,                               /* on_rx_response() */
    NULL,                               /* on_tx_request.   */
    NULL,                               /* on_tx_response() */
    NULL,                               /* on_tsx_state()   */
};

void on_incoming_call(pjsua_acc_id acc_id , pjsua_call_id call_id , pjsip_rx_data* rx_data)
{
    pjsua_call_info ci;
    pjsua_call_get_info(call_id, &ci);
    
    
    PJ_LOG(3, ("on_incoming_call", "Incoming call from %x*s", (int)ci.remote_info.slen, ci.remote_info.ptr));

    pjsua_call_answer(call_id, 200, NULL, NULL);
}

void on_call_media_state(pjsua_call_id callId)
{

}

void on_call_state(pjsua_call_id callId , pjsip_event* e)
{

}

void on_reg_start(pjsua_acc_id acc_id, pj_bool_t renew)
{
    PJ_LOG(3, ("on reg start", "Incoming reg"));
}

void on_reg_state(pjsua_acc_id acc_id)
{
    PJ_LOG(3, ("on reg state", "Incoming reg"));
}

int main()
{
    pj_status_t status;

    // initialize pjsua
    status = pjsua_create();

    // ensure the successful creation of the pjsua
    if (status != PJ_SUCCESS)
    {
        // TODO - pull this out into its own specialized method
        std::cout << "Error creating pjsua, exiting...";
        pjsua_destroy();
        return -1;
    }

    // create the config and set callbacks
    pjsua_config cfg;
    pjsua_config_default(&cfg);
    cfg.cb.on_incoming_call = &on_incoming_call;
    cfg.cb.on_call_media_state = &on_call_media_state;
    cfg.cb.on_call_state = &on_call_state;
    cfg.cb.on_reg_started = &on_reg_start;
    cfg.cb.on_reg_state = &on_reg_state;
    

    // log config
    pjsua_logging_config log_cfg;
    pjsua_logging_config_default(&log_cfg);
    log_cfg.console_level = 4;
    log_cfg.level = 4;
    log_cfg.log_filename = pj_str(const_cast<char*>("pjsip.log"));

    // media configuration
    pjsua_media_config media_cfg;
    pjsua_media_config_default(&media_cfg);
    pjmedia_transport pjmt;
    

    // initialize pjsua
    status = pjsua_init(&cfg, &log_cfg, &media_cfg);
    if (status != PJ_SUCCESS)
    {
        // TODO - pull this out into its own specialized method
        std::cout << "Error initializing pjsua, exiting...";
        pjsua_destroy();
        return -1;
    }

    // add udp transport
    pjsua_transport_config transport_cfg;
    pjsua_transport_config_default(&transport_cfg);
    transport_cfg.port = 5070;
    status = pjsua_transport_create(PJSIP_TRANSPORT_UDP, &transport_cfg, &transport_id);

    status = pjsip_endpt_register_module(pjsua_get_pjsip_endpt(), &mod_simple_registrar);
    if (status != PJ_SUCCESS)
    {
        // TODO - pull this out into its own specialized method
        std::cout << "Error creating registrar modle, exiting...";
        pjsua_destroy();
        return -1;
    }

    status = pjsua_start();
    if (status != PJ_SUCCESS)
    {
        // TODO - pull this out into its own specialized method
        std::cout << "Error starting pjsua, exiting...";
        pjsua_destroy();
        return -1;
    }
   
    // TODO delete this, its probably not necessary
    //pjsip_endpt_register_module(pjsua_get_pjsip_endpt(), pjsip_ua_instance());
    /*
    // add acc 
    pjsua_acc_config acc_cfg;
    pjsua_acc_config_default(&acc_cfg);
    acc_cfg.id = pj_str(const_cast<char*>("sip:server@localhoset"));
    // we are commenting out the reg_uri as to prevent an attempt at self registration
    //acc_cfg.reg_uri = pj_str(const_cast<char*>("sip:localhost"));
    acc_cfg.cred_count = 1;
    acc_cfg.cred_info[0].scheme = pj_str(const_cast<char*>("Digest"));
    acc_cfg.cred_info[0].realm = pj_str(const_cast<char*>("*"));
    acc_cfg.cred_info[0].username = pj_str(const_cast<char*>("user"));
    acc_cfg.cred_info[0].data_type = PJSIP_CRED_DATA_PLAIN_PASSWD;
    acc_cfg.cred_info[0].data = pj_str(const_cast<char*>("password"));

    pjsua_acc_id acc_id;
    status = pjsua_acc_add(&acc_cfg, PJ_TRUE, &acc_id);
    if (status != PJ_SUCCESS)
    {
        // TODO - pull this out into its own specialized method
        std::cout << "Error creating pjsua account, exiting...";
        pjsua_destroy();
        return -1;
    }

    // start pjsua
    status = pjsua_start();
    if (status != PJ_SUCCESS)
    {
        // TODO - pull this out into its own specialized method
        std::cout << "Error starting pjsua, exiting...";
        pjsua_destroy();
        return -1;
    }
    */
    PJ_LOG(3, ("Main" , "PJSUA started successfully!!!"));

    pj_thread_sleep(10000000);

    pjsua_destroy();

    return 0;

    /*
    // Initialize PJSUA2
    Endpoint ep;
    ep.libCreate();
    EpConfig ep_cfg;
    ep.libInit(ep_cfg);
    TransportConfig tcfg;
    tcfg.port = 5060;
    ep.transportCreate(PJSIP_TRANSPORT_UDP, tcfg);
    ep.libStart();

    // Create and configure account
    AccountConfig acfg;
    acfg.idUri = "sip:server@domain.com";
    acfg.regConfig.registrarUri = "sip:domain.com";
    Account acc;
    acc.create(acfg);

    // Wait for user input to exit
    std::cout << "Press Enter to exit..." << std::endl;
    std::cin.get();

    // Cleanup
    ep.libDestroy();
    return 0;
    */
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
