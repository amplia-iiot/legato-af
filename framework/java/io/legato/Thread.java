
// -------------------------------------------------------------------------------------------------
/**
 *  @file Thread.java
 *
 */
// -------------------------------------------------------------------------------------------------

package io.legato;

//--------------------------------------------------------------------------------------------------
/**
 *  This class is used to directly work with the Legato thread-specific structures needed by the
 *  framework.
 */
//--------------------------------------------------------------------------------------------------
public class Thread
{

    //----------------------------------------------------------------------------------------------
    /**
     *  Used to initialize the thread-specific data needed by the Legato framework for the calling
     *  thread.
     *
     *  @param threadName  Name for the thread.
     */
    //----------------------------------------------------------------------------------------------
    public static void initLegatoThreadData(String threadName)
    {
        LegatoJni.InitLegatoThreadData(threadName);
    }

    //----------------------------------------------------------------------------------------------
    /**
     *  Clean-up the thread-specific data that was initialized using
     *  initLegatoThreadData(threadName).
     */
    //----------------------------------------------------------------------------------------------
    public static void cleanupLegatoThreadData()
    {
        LegatoJni.CleanupLegatoThreadData();
    }
}
