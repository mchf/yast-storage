#include <iostream> 
#include <sstream> 

#include <ycp/y2log.h>

#include "y2storage/LvmVg.h"
#include "y2storage/LvmLv.h"
#include "y2storage/SystemCmd.h"
#include "y2storage/AppUtil.h"
#include "y2storage/Storage.h"

LvmVg::LvmVg( Storage * const s, const string& Name ) :
    Container(s,Name,staticType())
    {
    y2milestone( "construcing lvm vg %s", Name.c_str() );
    init();
    if( Name.size()>0 )
	{
	SystemCmd c( "/sbin/vgdisplay --units k -v " + Name );
	unsigned cnt = c.numLines();
	unsigned i = 0;
	string line;
	string tmp;
	string::size_type pos;
	while( i<cnt )
	    {
	    line = *c.getLine( i++ );
	    if( line.find( "Volume group" )!=string::npos )
		{
		line = *c.getLine( i++ );
		while( line.find( "Logical volume" )==string::npos &&
		       line.find( "Physical volume" )==string::npos &&
		       i<cnt )
		    {
		    line.erase( 0, line.find_first_not_of( " \t\n" ));
		    if( line.find( "Format" ) == 0 )
			{
			lvm1 = extractNthWord( 1, line )=="lvm1";
			}
		    if( line.find( "PE Size" ) == 0 )
			{
			tmp = extractNthWord( 2, line );
			pos = tmp.find( '.' );
			if( pos!=string::npos )
			    tmp.erase( pos );
			tmp >> pe_size;
			}
		    if( line.find( "Total PE" ) == 0 )
			{
			extractNthWord( 2, line ) >> num_pe;
			}
		    if( line.find( "Free  PE" ) == 0 )
			{
			extractNthWord( 4, line ) >> free_pe;
			}
		    if( line.find( "VG Status" ) == 0 )
			{
			status = extractNthWord( 2, line );
			}
		    if( line.find( "VG Access" ) == 0 )
			{
			ronly = extractNthWord( 2, line, true ).find( "write" ) == string::npos;
			}
		    if( line.find( "VG UUID" ) == 0 )
			{
			uuid = extractNthWord( 2, line );
			}
		    line = *c.getLine( i++ );
		    }
		string name;
		string uuid;
		string status;
		string allocation;
		unsigned long num_le = 0;
		bool readOnly = false;
		while( line.find( "Physical volume" )==string::npos && i<cnt )
		    {
		    line.erase( 0, line.find_first_not_of( " \t\n" ));
		    if( line.find( "LV Name" ) == 0 )
			{
			if( name.size()>0 )
			    {
			    addLv( num_le, name, uuid, status, allocation,
				   readOnly );
			    }
			name = extractNthWord( 2, line );
			if( (pos=name.rfind( "/" ))!=string::npos )
			    name.erase( 0, pos+1 );
			}
		    if( line.find( "LV Write Access" ) == 0 )
			{
			readOnly = extractNthWord( 3, line, true ).find( "only" ) != string::npos;
			}
		    if( line.find( "LV Status" ) == 0 )
			{
			status = extractNthWord( 2, line, true );
			}
		    if( line.find( "Current LE" ) == 0 )
			{
			extractNthWord( 2, line ) >> num_le;
			}
		    if( line.find( "Allocation" ) == 0 )
			{
			allocation = extractNthWord( 1, line );
			}
		    line = *c.getLine( i++ );
		    }
		if( name.size()>0 )
		    {
		    addLv( num_le, name, uuid, status, allocation, readOnly );
		    }
		Pv *p = new Pv;
		while( i<cnt )
		    {
		    line.erase( 0, line.find_first_not_of( " \t\n" ));
		    if( line.find( "PV Name" ) == 0 )
			{
			if( p->device.size()>0 )
			    {
			    addPv( p );
			    }
			p->device = extractNthWord( 2, line );
			}
		    if( line.find( "PV UUID" ) == 0 )
			{
			p->uuid = extractNthWord( 2, line );
			}
		    if( line.find( "PV Status" ) == 0 )
			{
			p->status = extractNthWord( 2, line );
			}
		    if( line.find( "Total PE" ) == 0 )
			{
			extractNthWord( 5, line ) >> p->num_pe;
			extractNthWord( 7, line ) >> p->free_pe;
			}
		    line = *c.getLine( i++ );
		    }
		if( p->device.size()>0 )
		    {
		    addPv( p );
		    }
		delete p;
		}
	    }
	}
    else
	{
	y2error( "empty name in constructor" );
	}
    }

LvmVg::LvmVg( Storage * const s, const string& file, bool ) :
    Container(s,"",staticType())
    {
    y2milestone( "construcing lvm vg %s from file %s", dev.c_str(), 
                 file.c_str() );
    }

LvmVg::~LvmVg()
    {
    y2milestone( "destructed lvm vg %s", dev.c_str() );
    }

void LvmVg::addLv( unsigned long& le, string& name, string& uuid,
                   string& status, string& alloc, bool& ro )
    {
    LvmLv *p = new LvmLv( *this, name, le, uuid, status, alloc );
    if( ro )
	p->setReadonly();
    addToList( p );
    name = uuid = status = alloc = "";
    le = 0; 
    ro = false;
    }

void LvmVg::addPv( Pv*& p )
    {
    pv.push_back( *p );
    getStorage()->setUsedBy( p->device, UB_LVM, name() );
    p = new Pv;
    }

int LvmVg::commitChanges( CommitStage stage )
    {
    y2milestone( "name %s stage %d", name().c_str(), stage );
    int ret = 0;
    switch( stage )
	{
	case DECREASE:
	    if( ret==0 )
		ret = Container::commitChanges( stage );
	    if( deleted() )
		{
		ret = doRemoveVg();
		}
	    break;
	case INCREASE:
	    if( created() )
		{
		ret = doCreateVg();
		}
	    if( ret==0 )
		{
		ret = Container::commitChanges( stage );
		}
	    break;
	default:
	    ret = Container::commitChanges( stage );
	    break;
	}
    y2milestone( "ret:%d", ret );
    return( ret );
    }

void LvmVg::getCommitActions( list<commitAction*>& l ) const
    {
    Container::getCommitActions( l );
    if( deleted() )
	{
	l.push_back( new commitAction( DECREASE, staticType(), 
				       removeVgText(false), true ));
	}
    else if( created() )
	{
	l.push_front( new commitAction( INCREASE, staticType(), 
				        createVgText(false), true ));
	}
    }

string 
LvmVg::removeVgText( bool doing ) const
    {
    string txt;
    if( doing )
        {
        // displayed text during action, %1$s is replaced by a name (e.g. system),
        txt = sformat( _("Removing Volume group %1$s"), name().c_str() );
        }
    else
        {
        // displayed text before action, %1$s is replaced by a name (e.g. system),
        txt = sformat( _("Remove Volume group %1$s"), name().c_str() );
        }
    return( txt );
    }

string 
LvmVg::createVgText( bool doing ) const
    {
    string txt;
    if( doing )
        {
        // displayed text during action, %1$s is replaced by a name (e.g. system),
        txt = sformat( _("Creating Volume group %1$s"), name().c_str() );
        }
    else
        {
        // displayed text before action, %1$s is replaced by a name (e.g. system),
        txt = sformat( _("Create Volume group %1$s"), name().c_str() );
        }
    return( txt );
    }

string LvmVg::metaString()
    {
    return( (lvm1)?"M1":"M2" );
    }

int LvmVg::doCreatePv( const string& device )
    {
    int ret = 0;
    y2milestone( "dev:%s", device.c_str() );
    SystemCmd c;
    string cmd = "mdadm --zero-superblock " + device;
    c.execute( cmd );
    cmd = "pvcreate -ff " + metaString() + " " + device;
    c.execute( cmd );
    if( c.retcode()!=0 )
	ret = LVM_CREATE_PV_FAILED;
    y2milestone( "ret:%d", ret );
    return( ret );
    }


void 
LvmVg::init()
    {
    pe_size = num_pe = free_pe = 0;
    lvm1 = false;
    }


void LvmVg::activate( bool val )
    {
    y2milestone( "old active:%d val:%d", lvm_active, val );
    if( lvm_active!=val )
	{
	SystemCmd c;
	if( lvm_active )
	    {
	    c.execute( "vgscan --mknodes" );
	    c.execute( "vgchange -a y" );
	    }
	else
	    {
	    c.execute( "vgchange -a n" );
	    }
	lvm_active = val;
	}
    }

void LvmVg::getVgs( list<string>& l )
    {
    l.clear();
    string vgname;
    string::size_type pos;
    SystemCmd c( "vgdisplay -s" );
    if( !lvm_active && c.numLines()>0 )
	lvm_active = true;
    for( int i=0; i<c.numLines(); ++i )
	{
	vgname = *c.getLine(i);
	pos=vgname.find_first_not_of( " \t\n\"" );
	if( pos>0 )
	    vgname.erase( 0, pos );
	pos=vgname.find_first_of( " \t\n\"" );
	if( pos>0 )
	    vgname.erase( pos );
	l.push_back(vgname);
	}
    std::ostringstream buf;
    buf << l;
    y2milestone( "detected Vgs %s", buf.str().c_str() );
    }

int LvmVg::doCreate( Volume* v ) { return( 0 ); }
int LvmVg::doRemove( Volume* v ) { return( 0 ); }
int LvmVg::doResize( Volume* v ) { return( 0 ); }

int
LvmVg::checkResize( Volume* v, unsigned long long newSizeK ) const
    {
    return( false );
    }

void LvmVg::logData( const string& Dir ) {;}

bool LvmVg::lvm_active = false;

