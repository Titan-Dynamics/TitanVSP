//
// This file is released under the terms of the NASA Open Source Agreement (NOSA)
// version 1.3 as detailed in the LICENSE file which accompanies this software.
//

// WingBodyFillet.h: Rolling ball fillets between wings and fuselages, built from slim wing sections.
//
//////////////////////////////////////////////////////////////////////

#if !defined(VSPWINGBODYFILLET__INCLUDED_)
#define VSPWINGBODYFILLET__INCLUDED_

#include <string>
#include <vector>

using std::string;
using std::vector;

class Vehicle;
class WingGeom;

namespace WingBodyFillet
{
    struct Settings
    {
        double m_Radius = 0.0;          // Fillet radius in model units.  <= 0 uses m_RadiusFrac.
        double m_RadiusFrac = 0.08;     // Fillet radius as a fraction of the exposed root chord.
        int m_NumSect = 14;             // Fillet sections inserted per wing.
    };

    // Fillets every wing whose root is buried in a fuselage.  Fillets made by an earlier call are
    // removed and rebuilt, so this can be run repeatedly.  Returns the number of wings filleted and
    // appends a line per wing to messages.
    int BlendAll( Vehicle* veh, vector< string > & messages, const Settings & settings = Settings() );

    // Removes the fillet sections from a wing and restores the panels they were split from.
    // Returns true if anything was removed.
    bool RemoveFillets( WingGeom* wing );
}

#endif // !defined(VSPWINGBODYFILLET__INCLUDED_)
