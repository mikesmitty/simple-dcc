#ifndef LCC_TRACTION_H
#define LCC_TRACTION_H

#include "openlcb/openlcb_types.h"

void lcc_traction_on_speed_changed(openlcb_node_t *node, uint16_t speed_float16);
void lcc_traction_on_function_changed(openlcb_node_t *node, uint32_t fn_addr, uint16_t fn_value);
void lcc_traction_on_emergency_entered(openlcb_node_t *node, train_emergency_type_enum emergency_type);
void lcc_traction_on_emergency_exited(openlcb_node_t *node, train_emergency_type_enum emergency_type);
openlcb_node_t *lcc_traction_on_search_no_match(uint16_t search_address, uint8_t flags);
void lcc_traction_on_controller_released(openlcb_node_t *node);

#endif // LCC_TRACTION_H
