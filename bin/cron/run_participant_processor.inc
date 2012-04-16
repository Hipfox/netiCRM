<?php

function run_participant_processor(){
  $obj = new CRM_ParticipantProcessor( );
  echo "Updating..";
  $obj->updateParticipantStatus( );
  echo "\nParticipant records updated. (Done)";
}
class CRM_ParticipantProcessor 
{
    function __construct( ) 
    {
    }
    
    public function updateParticipantStatus( )
    {
        require_once 'CRM/Event/PseudoConstant.php';
        $participantRole = CRM_Event_PseudoConstant::participantRole( );
        $pendingStatuses = CRM_Event_PseudoConstant::participantStatus( null, "class = 'Pending'"  );
        $expiredStatuses = CRM_Event_PseudoConstant::participantStatus( null, "class = 'Negative'" );
        $waitingStatuses = CRM_Event_PseudoConstant::participantStatus( null, "class = 'Waiting'"  );
        
        //build the required status ids.
        $statusIds =  '(' . implode( ',', array_merge( array_keys( $pendingStatuses ), array_keys( $waitingStatuses ) ) ) . ')';
        
        $participantDetails = $fullEvents = array( );
        $expiredParticipantCount = $waitingConfirmCount = $waitingApprovalCount = 0;
        
        //get all participant who's status in class pending and waiting
        $query = "SELECT * FROM civicrm_participant WHERE status_id IN {$statusIds} ORDER BY register_date";
        
        $query = "
   SELECT  participant.id,
           participant.contact_id,
           participant.status_id,
           participant.register_date,
           participant.registered_by_id,
           participant.event_id,
           event.title as eventTitle,
           event.expiration_time,
           event.requires_approval
     FROM  civicrm_participant participant
LEFT JOIN  civicrm_event event ON ( event.id = participant.event_id )
    WHERE  participant.status_id IN {$statusIds}
 ORDER BY  participant.register_date, participant.id 
";
        $dao =& CRM_Core_DAO::executeQuery( $query );
        while ( $dao->fetch( ) ) {
            
            $participantDetails[$dao->id] = array( 'id'               => $dao->id,
                                                   'event_id'         => $dao->event_id,
                                                   'status_id'        => $dao->status_id,
                                                   'contact_id'       => $dao->contact_id,
                                                   'register_date'    => $dao->register_date,
                                                   'registered_by_id' => $dao->registered_by_id,
                                                   'eventTitle'       => $dao->eventTitle,
                                                   'expiration_time'  => $dao->expiration_time,
                                                   'requires_approval'=> $dao->requires_approval
                                                   );
        }
        
        if ( !empty( $participantDetails ) ) {
            //cron 1. move participant from pending to expire if needed
            foreach ( $participantDetails as $participantId => $values ) {
                //process the additional participant at the time of
                //primary participant, don't process separately.
                if ( CRM_Utils_Array::value( 'registered_by_id', $values ) ) {
                    continue;
                }
                
                $expirationTime = CRM_Utils_Array::value( 'expiration_time', $values );
                if ( $expirationTime && array_key_exists( $values['status_id'], $pendingStatuses ) ) { 
                    
                    //get the expiration and registration pending time.
                    $expirationSeconds = $expirationTime * 3600;
                    $registrationPendingSeconds = CRM_Utils_Date::unixTime( $values['register_date'] );
                    
                    // expired registration since registration cross allow confirmation time.
                    if ( ( $expirationSeconds + $registrationPendingSeconds ) < time( ) ) {
                        
                        //lets get the transaction mechanism.
                        require_once 'CRM/Core/Transaction.php';
                        $transaction = new CRM_Core_Transaction( );
                        
                        require_once 'CRM/Event/BAO/Participant.php';
                        $ids = array( $participantId );
                        $expiredId = array_search( 'Expired', $expiredStatuses );
                        $results = CRM_Event_BAO_Participant::transitionParticipants( $ids, $expiredId, $values['status_id'], true, true );
                        $transaction->commit( ); 
                        
                        if ( !empty( $results ) ) {
                            //diaplay updated participants
                            if ( is_array( $results['updatedParticipantIds'] ) && !empty( $results['updatedParticipantIds'] ) ) {
                                foreach ( $results['updatedParticipantIds'] as $processedId ) {
                                    $expiredParticipantCount += 1;   
                                    echo "\n\n- status updated to: Expired";
                                    
                                    //mailed participants.
                                    if ( is_array( $results['mailedParticipants'] ) && 
                                         array_key_exists( $processedId, $results['mailedParticipants']) ) {
                                        echo "\nExpiration Mail sent to: {$results['mailedParticipants'][$processedId]}";
                                    }
                                }
                            }
                        }
                    }
                }
            }//cron 1 end. 
            
            //cron 2. lets move participants from waiting list to pending status
            foreach ( $participantDetails as $participantId => $values ) {
                //process the additional participant at the time of
                //primary participant, don't process separately.
                if ( CRM_Utils_Array::value( 'registered_by_id', $values ) ) {
                    continue;
                }
                
                if ( array_key_exists( $values['status_id'], $waitingStatuses ) && 
                     !array_key_exists( $values['event_id'], $fullEvents ) ) {
                    
                    if ( $waitingStatuses[$values['status_id']] == 'On waitlist' ) {
                        
                        //check the target event having space.
                        require_once 'CRM/Event/BAO/Participant.php';
                        $eventOpenSpaces = CRM_Event_BAO_Participant::eventFull( $values['event_id'], true, false );
                        
                        if ( $eventOpenSpaces && is_numeric( $eventOpenSpaces ) ||  ( $eventOpenSpaces === null ) ) {
                            
                            //get the additional participant if any.
                            $additionalIds = CRM_Event_BAO_Participant::getAdditionalParticipantIds( $participantId );
                            
                            $allIds = array( $participantId );
                            if ( !empty( $additionalIds ) ) $allIds = array_merge( $allIds, $additionalIds );
                            $requiredSpaces = CRM_Event_BAO_Participant::totalEventSeats( $allIds );
                            
                            //need to check as to see if event has enough speces
                            if ( ( $requiredSpaces <= $eventOpenSpaces ) || ( $eventOpenSpaces === null ) ) {
                                require_once 'CRM/Core/Transaction.php';
                                $transaction = new CRM_Core_Transaction( );
                                
                                require_once 'CRM/Event/BAO/Participant.php';
                                $ids = array( $participantId );
                                $updateStatusId = array_search( 'Pending from waitlist', $pendingStatuses );
                                
                                //lets take a call to make pending or need approval
                                if ( $values['requires_approval'] ) {
                                    $updateStatusId = array_search( 'Awaiting approval', $waitingStatuses );
                                }
                                $results = CRM_Event_BAO_Participant::transitionParticipants( $ids, $updateStatusId, 
                                                                                              $values['status_id'], true, true );
                                //commit the transaction.
                                $transaction->commit( );
                                
                                if ( !empty( $results ) ) {
                                    //diaplay updated participants
                                    if ( is_array( $results['updatedParticipantIds'] ) && 
                                         !empty( $results['updatedParticipantIds'] ) ) {
                                        foreach ( $results['updatedParticipantIds'] as $processedId ) {
                                            if ( $values['requires_approval'] ) {
                                                $waitingApprovalCount += 1;
                                                echo "\nstatus updated to: Awaiting approval";
                                                echo "\nWill send you Confirmation Mail when registration get approved.";
                                            } else {
                                                $waitingConfirmCount += 1;  
                                                echo "\n\n- status updated to: Pending from waitlist";
                                                if ( is_array( $results['mailedParticipants'] ) && 
                                                     array_key_exists( $processedId, $results['mailedParticipants']) ) {
                                                    echo "\nConfirmation Mail sent to: {$results['mailedParticipants'][$processedId]}";
                                                }
                                            }
                                        }
                                    }
                                }
                            } else {
                                //target event is full.
                                $fullEvents[$values['event_id']] = $values['eventTitle'];
                            }
                        } else {
                            //target event is full.
                            $fullEvents[$values['event_id']] = $values['eventTitle'];
                        }
                    }
                }
            }//cron 2 ends.
        }
        
        echo "\n\nNumber of Expired registration(s) = {$expiredParticipantCount}";
        echo "\nNumber of registration(s) require approval =  {$waitingApprovalCount}";
        echo "\nNumber of registration changed to Pending from waitlist = {$waitingConfirmCount}\n\n";
        if ( !empty( $fullEvents ) ) {
            foreach ( $fullEvents as $eventId => $title ) {
                echo "Full Event : {$title}\n";
            }
        }
    }
}
?>