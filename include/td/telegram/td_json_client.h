#ifndef TD_TELEGRAM_TD_JSON_CLIENT_H
#define TD_TELEGRAM_TD_JSON_CLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

int td_create_client_id(void);
void td_send(int client_id, const char *request);
const char *td_receive(double timeout);
const char *td_execute(const char *request);

#ifdef __cplusplus
}
#endif

#endif
