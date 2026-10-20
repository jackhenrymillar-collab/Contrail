#include "ReducedMotion.h"

#import <AppKit/AppKit.h>

namespace contrail::ui
{

bool prefersReducedMotion()
{
    return [[NSWorkspace sharedWorkspace] accessibilityDisplayShouldReduceMotion];
}

} // namespace contrail::ui
