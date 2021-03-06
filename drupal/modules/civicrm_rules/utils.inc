<?php

/*
 +--------------------------------------------------------------------+
 | CiviCRM version 3.3                                                |
 +--------------------------------------------------------------------+
 | Copyright CiviCRM LLC (c) 2004-2010                                |
 +--------------------------------------------------------------------+
 | This file is a part of CiviCRM.                                    |
 |                                                                    |
 | CiviCRM is free software; you can copy, modify, and distribute it  |
 | under the terms of the GNU Affero General Public License           |
 | Version 3, 19 November 2007.                                       |
 |                                                                    |
 | CiviCRM is distributed in the hope that it will be useful, but     |
 | WITHOUT ANY WARRANTY; without even the implied warranty of         |
 | MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.               |
 | See the GNU Affero General Public License for more details.        |
 |                                                                    |
 | You should have received a copy of the GNU Affero General Public   |
 | License and the CiviCRM Licensing Exception along                  |
 | with this program; if not, contact CiviCRM LLC                     |
 | at info[AT]civicrm[DOT]org. If you have questions about the        |
 | GNU Affero General Public License or the licensing of CiviCRM,     |
 | see the CiviCRM license FAQ at http://civicrm.org/licensing        |
 +--------------------------------------------------------------------+
*/

/**
 *
 * @package CRM
 * @copyright CiviCRM LLC (c) 2004-2010
 * $Id$
 *
 */

/**
 * This file contains functions that calls civicrm apis for data manupulation / retrieval
 */

/**
 * Function to get the list of groups
 */
function _civicrm_get_groups( ) {
	civicrm_initialize( );
	require_once 'api/v2/Group.php';
	$params = array( 'return.id' => 1, 'return.title' => 1 );
	$groupInformation = civicrm_group_get( $params );

	$groups = array( );	
	foreach ( $groupInformation as $values ) {
		$groups[ $values['id'] ] = $values['title'];
	}
	
	return $groups;
}	

/**
 * Function to add contacts to group
 */
function _civicrm_rules_group_contact( $contactId, $groupId, $action = 'add' ) {	
	civicrm_initialize( );
	require_once 'api/v2/GroupContact.php';
	$params = array( 'contact_id' => $contactId,
									 'group_id' => $groupId
								 );
	civicrm_group_contact_common( $params, $action );
}

/**
 * Loads a contact into a version suitable for the rules actions
 */
function _civicrm_rules_load_contact( $contactID ) {
    require_once 'CRM/Contact/DAO/Contact.php';
    $contact = new CRM_Contact_DAO_Contact( );
    $contact->id = $contactID;

    if ( $contact->id ) {
        // load the contact data here
        $contact->find( true );
    }
    
    return $contact;
}

function _civicrm_rules_in_group( $contact, $group ) {
    // check if contact is in the group gid
    return true;
}

