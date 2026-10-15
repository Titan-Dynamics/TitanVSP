//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//
//////////////////////////////////////////////////////////////////////

#include <stdlib.h>
#include <stdio.h>
#include <FL/Fl.H>
#include <FL/Fl_Tooltip.H>
#include <FL/Fl_Window.H>
#include <FL/platform.H>

#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#include <set>
#pragma comment( lib, "dwmapi.lib" )
#endif
#include "viewerUI.H"

int ForceView = 0;
int TimeStamp = 0;
int Calculix = 0;
char TimeLabel[2000];
char CalculixFileName[2000];
float BoxSize;

#define VER_MAJOR 6
#define VER_MINOR 4
#define VER_PATCH 1

/*##############################################################################
#                                                                              #
#                                 viewer main                                  #
#                                                                              #
##############################################################################*/

//==== TitanVSP dark theme ====//
// The same palette as TitanVSP's own windows: dark background and gray ramp, darker inputs and
// lists, light text, blue selection.  The viewer colours its buttons and frames with a few fixed
// palette entries; those become muted versions of the same hues.
static void ApplyDarkTheme()
{
    Fl::scheme( "GTK+" );

    Fl::background( 48, 50, 54 );
    Fl::background2( 33, 35, 38 );
    Fl::foreground( 222, 224, 228 );

    Fl::set_color( FL_SELECTION_COLOR, 74, 128, 204 );
    Fl::set_color( FL_INACTIVE_COLOR, 128, 130, 136 );
    Fl::set_color( FL_BLACK, 222, 224, 228 );

    Fl::set_color( ( Fl_Color )2, 62, 128, 84 );        // Toggles when on.
    Fl::set_color( ( Fl_Color )4, 52, 84, 140 );        // Buttons, the view's frame, list selection.
    Fl::set_color( ( Fl_Color )5, 104, 72, 128 );       // Draw toggles.
    Fl::set_color( ( Fl_Color )6, 56, 116, 126 );
    Fl::set_color( ( Fl_Color )23, 40, 42, 46 );        // Read-only fields.
    Fl::set_color( ( Fl_Color )231, 74, 128, 204 );     // Menu highlight.

    Fl_Tooltip::color( fl_rgb_color( 30, 32, 35 ) );
    Fl_Tooltip::textcolor( fl_rgb_color( 222, 224, 228 ) );
}

// Windows draws title bars light unless asked; ask each window, once, for a dark one.
static void DarkTitleBars( void * )
{
#ifdef _WIN32
    static std::set< HWND > done;
    for ( Fl_Window* win = Fl::first_window(); win; win = Fl::next_window( win ) )
    {
        HWND hwnd = fl_win32_xid( win );
        if ( !hwnd || done.count( hwnd ) )
        {
            continue;
        }
        done.insert( hwnd );
        BOOL dark = TRUE;
        const DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_ATTR = 20;
        DwmSetWindowAttribute( hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE_ATTR, &dark, sizeof( dark ) );
        SetWindowPos( hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED );
    }
#endif
    Fl::repeat_timeout( 0.5, DarkTitleBars );
}

int main(int argc, char **argv) {

    int i;

    ApplyDarkTheme();

    viewerUI *vui = new viewerUI;

    // Initial global objects.

    //  Fl::visual(FL_DOUBLE|FL_INDEX);

    // Parse the input

    char *name = argv[argc-1];

    i = 1;

    while ( i <= argc - 2 ) {

	   if ( strcmp(argv[i],"-forceview") == 0 ) {

          ForceView = 1;

          BoxSize = atof(argv[++i]);

	   }

	   else if ( strcmp(argv[i],"-timestamp") == 0 ) {

          TimeStamp = 1;


	   }
      
	   if ( strcmp(argv[i],"-calculix") == 0 ) {

          Calculix = 1;

          snprintf(CalculixFileName,sizeof(CalculixFileName)*sizeof(char),"%s",argv[++i]);
          
          printf("CalculixFileName: %s \n",CalculixFileName);fflush(NULL);

	   }      

	   else {
         printf( "VSPVIEWER v.%d.%d.%d --- %s \n", VER_MAJOR, VER_MINOR, VER_PATCH, __DATE__ );
         printf("Please direct questions to David Kinney, David.J.Kinney@nasa.gov \n");
	      printf("Unrecognized option! \n");
	      printf("Usage: \n");
	      printf("viewer [OPTIONS] filename \n");

	   }

       i++;

    }

    if ( argc <= 1 ) {

       printf( "VSPVIEWER v.%d.%d.%d --- %s \n", VER_MAJOR, VER_MINOR, VER_PATCH, __DATE__ );
       printf("Please direct questions to David Kinney, David.J.Kinney@nasa.gov \n");
       printf("Unrecognized option! \n");
       printf("Usage: \n");
	    printf("viewer [OPTIONS] filename \n");

       exit(1);

    }

    // Pass a pointer to the vui object to the glviewer object

    vui->glviewer->PassTheVui(vui);
    
    // Pass a pointer to the geometry list box
    
    vui->glviewer->PassThePanelComGeomTagsBrowser(vui->PanelComGeomTagsBrowser);
    
    // Pass a pointer to the cutting planes list box
    
    vui->glviewer->PassTheCuttingPlanesBrowser(vui->CutPlanesBrowser);

    // Force viewing box

    if ( ForceView ) vui->glviewer->FixViewBox(BoxSize);

    // Set a time stamp label

    if ( TimeStamp ) vui->glviewer->SetTimeStampLabel(TimeLabel, sizeof(TimeLabel)*sizeof(char));
    
    // Set the Calculix file name
    
    if ( Calculix ) {
       
       printf("Setting calculix file name for viewer... \n");fflush(NULL);
       
       vui->glviewer->SetCalculixFileName(CalculixFileName, sizeof(CalculixFileName)*sizeof(char));
       
    }
    
    // Load in the geometry and TPS data

    vui->LoadInitialData(name);

    // Strip off all but the first argument of the command line

    argc = 1;

    vui->show(argc, argv);

    Fl::add_timeout( 0.0, DarkTitleBars );

    return Fl::run();
}

