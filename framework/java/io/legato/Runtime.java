
// -------------------------------------------------------------------------------------------------
/**
 *  @file Runtime.java
 *
 *  Copyright (C) Sierra Wireless, Inc. 2014. All rights reserved.
 *  Use of this work is subject to license.
 */
// -------------------------------------------------------------------------------------------------

package io.legato;




//--------------------------------------------------------------------------------------------------
/**
 *  This class is used to directly work with the Legato runtime.
 */
//--------------------------------------------------------------------------------------------------
public class Runtime
{
    //----------------------------------------------------------------------------------------------
    /**
     *  .
     */
    //----------------------------------------------------------------------------------------------
    public static void connectToLogControl()
    {
    }

    //----------------------------------------------------------------------------------------------
    /**
     *  Used to queue a Legato component's init function to run once the event loop as been started.
     *
     *  @param component  The component to run.
     */
    //----------------------------------------------------------------------------------------------
    public static void scheduleComponentInit(Component component)
    {
        LegatoJni.ScheduleComponentInit(component);
    }

    //----------------------------------------------------------------------------------------------
    /**
     *  Kick off the Legato event loop in the current thread.  This method will not return from the
     *  loop.
     */
    //----------------------------------------------------------------------------------------------
    public static void runEventLoop()
    {
        LegatoJni.RunLoop();
    }

    //----------------------------------------------------------------------------------------------
    /**
     *  Services the Legato event loop in the current Thread. This method will only perform one
     *  servicing step before returning, regardless of how much work there is to do. It is the
     *  caller's responsability to check the return code and keep calling until it indicates that
     *  there is no more work to be done.
     *
     *  @warning Only intended for use when new threads need to be served by the event loop, due to
     *           events or timers they need to handle. The preferred approach is to call
     *           runEventLoop()
     *
     *  @return
     *   - Result.OK if there is more to be done. DO NOT GO BACK TO SLEEP without calling
     *               serviceEventLoop() again.
     *   - Result.WOULD_BLOCK if there is nothing left to do for now and it is safe to go back to
     *                        sleep.
     */
    //----------------------------------------------------------------------------------------------
    public static Result serviceEventLoop()
    {
        return LegatoJni.ServiceLoop();
    }

    //----------------------------------------------------------------------------------------------
    /**
     *  Executes a Runnable in the main Legato thread. This function returns immediately as it 
     *  only queues the request.
     *
     *  @param job  The Runnable to run.
     */
    //----------------------------------------------------------------------------------------------
    public static void QueueFunctionInMainThread(Runnable job)
    {
        LegatoJni.QueueFunctionInMainThread(job);
    }
}
