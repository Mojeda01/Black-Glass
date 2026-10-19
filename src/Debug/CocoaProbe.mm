#include "CocoaProbe.h"
#include "DisplayTiming.h"

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

#import <Cocoa/Cocoa.h>
#import <CoreVideo/CoreVideo.h>
#import <QuartzCore/CAMetalLayer.h>

#include <cstdio>
#include <cstring>

namespace
{
    CVDisplayLinkRef g_link = nullptr;
    CGDirectDisplayID g_linkDisplay = 0;

    CVReturn displayLinkCallback(CVDisplayLinkRef, const CVTimeStamp*, const CVTimeStamp* outputTime,
                                 CVOptionFlags, CVOptionFlags*, void*)
    {
        const double frequency = static_cast<double>(glfwGetTimerFrequency());
        const double hostNow = static_cast<double>(glfwGetTimerValue());
        const double seconds = glfwGetTime() + (static_cast<double>(outputTime->hostTime) - hostNow) / frequency;
        DisplayTiming::pushVblank(seconds);
        return kCVReturnSuccess;
    }

    void ensureDisplayLink(CGDirectDisplayID display)
    {
        if (display == 0)
        {
            return;
        }
        if (g_link == nullptr)
        {
            if (CVDisplayLinkCreateWithCGDisplay(display, &g_link) != kCVReturnSuccess)
            {
                g_link = nullptr;
                return;
            }
            CVDisplayLinkSetOutputCallback(g_link, displayLinkCallback, nullptr);
            g_linkDisplay = display;
            CVDisplayLinkStart(g_link);
        }
        else if (display != g_linkDisplay)
        {
            CVDisplayLinkSetCurrentCGDisplay(g_link, display);
            g_linkDisplay = display;
        }
    }

    void copyClean(char* destination, size_t capacity, const char* source)
    {
        std::strncpy(destination, source, capacity - 1);
        destination[capacity - 1] = '\0';
        for (char* c = destination; *c != '\0'; c++)
        {
            if (*c == ',' || *c == '\n')
            {
                *c = ' ';
            }
        }
    }
}

bool probeCocoaWindow(GLFWwindow* handle, CocoaProbeInfo& info)
{
    @autoreleasepool
    {
        NSWindow* window = glfwGetCocoaWindow(handle);
        if (window == nil)
        {
            return false;
        }

        info.available = true;
        info.fullscreen = (window.styleMask & NSWindowStyleMaskFullScreen) != 0;
        info.visible = (window.occlusionState & NSWindowOcclusionStateVisible) != 0;
        info.scale = window.backingScaleFactor;
        info.appActive = NSApp.isActive;
        info.keyWindow = window.isKeyWindow;
        info.miniaturized = window.isMiniaturized;
        info.onActiveSpace = window.isOnActiveSpace;

        NSArray<NSScreen*>* screens = NSScreen.screens;
        info.screenCount = static_cast<int>(screens.count);

        const NSRect frame = window.frame;
        const double windowArea = frame.size.width * frame.size.height;
        double onOwnScreen = 0.0;
        info.screensTouched = 0;
        for (NSScreen* entry in screens)
        {
            const NSRect overlap = NSIntersectionRect(frame, entry.frame);
            const double area = overlap.size.width * overlap.size.height;
            if (area > 0.0)
            {
                info.screensTouched++;
            }
            if (entry == window.screen)
            {
                onOwnScreen = area;
            }
        }
        info.screenFraction = windowArea > 0.0 ? onOwnScreen / windowArea : 0.0;

        NSMutableString* list = [NSMutableString string];
        for (NSScreen* entry in screens)
        {
            int hz = 0;
            if (@available(macOS 12.0, *))
            {
                hz = static_cast<int>(entry.maximumFramesPerSecond);
            }
            [list appendFormat:@"%@%@@%d", list.length > 0 ? @"|" : @"", entry.localizedName, hz];
        }
        copyClean(info.screens, sizeof(info.screens), list.UTF8String);

        NSScreen* screen = window.screen;
        if (screen != nil)
        {
            info.primary = (screen == screens.firstObject);

            if (@available(macOS 12.0, *))
            {
                info.screenMaxHz = static_cast<int>(screen.maximumFramesPerSecond);
            }

            const char* name = screen.localizedName.UTF8String;
            if (name != nullptr)
            {
                copyClean(info.screenName, sizeof(info.screenName), name);
            }

            NSNumber* number = screen.deviceDescription[@"NSScreenNumber"];
            if (number != nil)
            {
                const CGDirectDisplayID display = static_cast<CGDirectDisplayID>(number.unsignedIntValue);
                info.displayId = display;
                CGDisplayModeRef mode = CGDisplayCopyDisplayMode(display);
                if (mode != nullptr)
                {
                    info.modeW = static_cast<int>(CGDisplayModeGetPixelWidth(mode));
                    info.modeH = static_cast<int>(CGDisplayModeGetPixelHeight(mode));
                    info.modeHz = CGDisplayModeGetRefreshRate(mode);
                    CGDisplayModeRelease(mode);
                }
                ensureDisplayLink(display);
            }
        }

        if (g_link != nullptr)
        {
            const CVTime nominal = CVDisplayLinkGetNominalOutputVideoRefreshPeriod(g_link);
            if ((nominal.flags & kCVTimeIsIndefinite) == 0 && nominal.timeScale > 0)
            {
                info.linkNominalMs = 1000.0 * static_cast<double>(nominal.timeValue) / static_cast<double>(nominal.timeScale);
            }
            info.linkActualMs = 1000.0 * CVDisplayLinkGetActualOutputVideoRefreshPeriod(g_link);
        }

        CALayer* layer = window.contentView.layer;
        if ([layer isKindOfClass:[CAMetalLayer class]])
        {
            CAMetalLayer* metal = static_cast<CAMetalLayer*>(layer);
            info.displaySync = metal.displaySyncEnabled;
            info.opaque = metal.opaque;
            info.drawableCount = static_cast<int>(metal.maximumDrawableCount);
            info.presentsWithTransaction = metal.presentsWithTransaction;
            info.drawableTimeout = metal.allowsNextDrawableTimeout;
            info.layerW = metal.drawableSize.width;
            info.layerH = metal.drawableSize.height;
        }

        return true;
    }
}

void shutdownCocoaProbe()
{
    if (g_link != nullptr)
    {
        CVDisplayLinkStop(g_link);
        CVDisplayLinkRelease(g_link);
        g_link = nullptr;
    }
}
