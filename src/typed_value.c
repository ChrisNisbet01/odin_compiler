#include "typed_value.h"

#include <string.h>

TypedValue const NullTypedValue = {0};

TypedValue
create_typed_value(LLVMValueRef val, TypeDescriptor const * desc, bool is_lvalue)
{
    TypedValue tv;
    memset(&tv, 0, sizeof(tv));
    tv.value = val;
    tv.type_info = desc;
    tv.is_lvalue = is_lvalue;
    return tv;
}

bool
typed_value_switch_to_pointee(TypedValue * tv)
{
    (void)tv;
    // TODO: implement when needed
    return false;
}
