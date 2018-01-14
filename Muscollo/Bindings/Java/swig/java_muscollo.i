%module(directors="1") opensimMuscollo

%include java_exception.i

%{
#include <Bindings/OpenSimHeaders_common.h>
#include <Bindings/OpenSimHeaders_simulation.h>
#include <Bindings/OpenSimHeaders_actuators.h>
#include <Bindings/OpenSimHeaders_analyses.h>
#include <Bindings/OpenSimHeaders_tools.h>
#include <Bindings/OpenSimHeaders_muscollo.h>
using namespace OpenSim;
using namespace SimTK;
%}

/* Load the required libraries when this module is loaded.                    */
/* TODO be more clever about detecting location of library. */
%pragma(java) jniclassclassmodifiers="public class"
SWIG_JAVABODY_PROXY(public, public, SWIGTYPE)
%pragma(java) jniclassimports="import javax.swing.JOptionPane;import java.awt.GraphicsEnvironment;"
%pragma(java) jniclasscode=%{
  static {
      try{
          // All OpenSim classes required for GUI operation.
          System.loadLibrary("osimMuscolloJavaJNI");
      }
      catch(UnsatisfiedLinkError e){
          String OS = System.getProperty("os.name").toLowerCase();
          String tip = "";
          if (OS.indexOf("win") >= 0) {
              tip = "\nMake sure Muscollo's bin directory is on your PATH.";
          } else if (OS.indexOf("mac") >= 0) {
              // Nothing for now; our use of RPATH means we were probably able
              // to locate the OpenSim dynamic libraries.
          } else /* linux */ {
              // Nothing for now; our use of RPATH means we were probably able
              // to locate the OpenSim dynamic libraries.
          }
          String msg = new String(
                  "Failed to load one or more dynamic libraries for Muscollo.\n"
                  + e + tip);

          String javaHome = System.getProperties().getProperty("java.home");
          boolean inMatlab = javaHome.toLowerCase().indexOf("matlab") >= 0;
          if (inMatlab) {
              msg +=  "\nSee https://simtk-confluence.stanford.edu/display/OpenSim/Scripting+with+Matlab";
          }

          System.out.println(msg);
          String title = "Error: Failed to load OpenSim Muscollo libraries";
          if (!GraphicsEnvironment.isHeadless()) {
              new JOptionPane(msg, JOptionPane.ERROR_MESSAGE)
                    .createDialog(null, title).setVisible(true);
          }
      }
  }
%}

%typemap(javacode) OpenSim::MucoPhase %{
    public static MucoBounds convertArrayToMB(double[] arr) throws Exception {
        MucoBounds bounds = new MucoBounds();
        if (arr.length > 2) {
            throw new RuntimeException(
                    "Bounds cannot have more than 2 elements.");
        } else if (arr.length == 1) {
            bounds = new MucoBounds(arr[0]);
        } else if (arr.length == 2) {
            bounds = new MucoBounds(arr[0], arr[1]);
        }
        return bounds;
    }
    public static MucoInitialBounds convertArrayToMIB(double[] arr)
            throws Exception {
        MucoInitialBounds bounds = new MucoInitialBounds();
        if (arr.length > 2) {
            throw new RuntimeException(
                    "Bounds cannot have more than 2 elements.");
        } else if (arr.length == 1) {
            bounds = new MucoInitialBounds(arr[0]);
        } else if (arr.length == 2) {
            bounds = new MucoInitialBounds(arr[0], arr[1]);
        }
        return bounds;
    }
    public static MucoFinalBounds convertArrayToMFB(double[] arr)
            throws Exception {
        MucoFinalBounds bounds = new MucoFinalBounds();
        if (arr.length > 2) {
            throw new RuntimeException(
                    "Bounds cannot have more than 2 elements.");
        } else if (arr.length == 1) {
            bounds = new MucoFinalBounds(arr[0]);
        } else if (arr.length == 2) {
            bounds = new MucoFinalBounds(arr[0], arr[1]);
        }
        return bounds;
    }

    public void setTimeBounds(double[] ib, double[] fb) throws Exception {
        setTimeBounds(this.convertArrayToMIB(ib), this.convertArrayToMFB(fb));
    }
    public void setStateInfo(String name, double[] b) throws Exception {
        setStateInfo(name, this.convertArrayToMB(b));
    }
    public void setStateInfo(String name, double[] b, double[] ib)
        throws Exception {
        setStateInfo(name, this.convertArrayToMB(b),
                this.convertArrayToMIB(ib));
    }
    public void setStateInfo(String name, double[] b, double[] ib, double[] fb)
        throws Exception {
        setStateInfo(name, this.convertArrayToMB(b),
                this.convertArrayToMIB(ib), this.convertArrayToMFB(fb));
    }

    public void setControlInfo(String name, double[] b) throws Exception {
        setControlInfo(name, this.convertArrayToMB(b));
    }
    public void setControlInfo(String name, double[] b, double[] ib)
        throws Exception {
        setControlInfo(name, this.convertArrayToMB(b),
                this.convertArrayToMIB(ib));
    }
    public void setControlInfo(String name, double[] b, double[] ib, double[] fb)
        throws Exception {
        setControlInfo(name, this.convertArrayToMB(b),
                this.convertArrayToMIB(ib), this.convertArrayToMFB(fb));
    }
%}

%typemap(javacode) OpenSim::MucoProblem %{
    public void setTimeBounds(double[] ib, double[] fb) throws Exception {
        setTimeBounds(MucoPhase.convertArrayToMIB(ib),
            MucoPhase.convertArrayToMFB(fb));
    }
    public void setStateInfo(String name, double[] b)
        throws Exception {
        setStateInfo(name, MucoPhase.convertArrayToMB(b));
    }
    public void setStateInfo(String name, double[] b, double[] ib)
        throws Exception {
        setStateInfo(name, MucoPhase.convertArrayToMB(b),
                MucoPhase.convertArrayToMIB(ib));
    }
    public void setStateInfo(String name, double[] b, double[] ib, double[] fb)
        throws Exception {
        setStateInfo(name, MucoPhase.convertArrayToMB(b),
                MucoPhase.convertArrayToMIB(ib), 
                MucoPhase.convertArrayToMFB(fb));
    }

    public void setControlInfo(String name, double[] b)
        throws Exception {
        setControlInfo(name, MucoPhase.convertArrayToMB(b));
    }
    public void setControlInfo(String name, double[] b, double[] ib)
        throws Exception {
        setControlInfo(name, MucoPhase.convertArrayToMB(b),
                MucoPhase.convertArrayToMIB(ib));
    }
    public void setControlInfo(String name, double[] b, double[] ib,
            double[] fb)
        throws Exception {
        setControlInfo(name, MucoPhase.convertArrayToMB(b),
                MucoPhase.convertArrayToMIB(ib), 
                MucoPhase.convertArrayToMFB(fb));
    }
%}

/* SWIG does not support initializer_list, but we can use Java arrays to
 * achieve similar syntax in MATLAB.
 * TODO create Vector(double[]) constructor. */
%typemap(javacode) OpenSim::MucoIterate %{
    public void setTime(double[] time) {
        Vector v = new Vector();
        v.resize(time.length);
        for (int i = 0; i < time.length; ++i) { v.set(i, time[i]); }
        setTime(v);
    }
    public void setState(String name, double[] traj) {
        Vector v = new Vector();
        v.resize(traj.length);
        for (int i = 0; i < traj.length; ++i) { v.set(i, traj[i]); }
        setState(name, v);
    }
    public void setControl(String name, double[] traj) {
        Vector v = new Vector();
        v.resize(traj.length);
        for (int i = 0; i < traj.length; ++i) { v.set(i, traj[i]); }
        setControl(name, v);
    }
%}

%import "java_actuators.i"

%include <Bindings/muscollo.i>

