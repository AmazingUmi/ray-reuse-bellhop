PROGRAM ArrivalAccumulatorProbe
  ! Direct component oracle for ArrMod::AddArr.  The fixed scenarios below
  ! deliberately exercise the storage and grouping branches without changing
  ! the normal Bellhop executable or ArrMod itself.
  USE ArrMod
  IMPLICIT NONE

  INTEGER :: scenario

  WRITE( *, '(A)' ) 'I8_ARRIVAL_ACCUMULATOR_PROBE_V1'
  DO scenario = 1, 15
     CALL RunScenario( scenario )
  END DO

CONTAINS

  SUBROUTINE Reset( capacity )
    INTEGER, INTENT( IN ) :: capacity
    IF ( ALLOCATED( NArr ) ) DEALLOCATE( NArr )
    IF ( ALLOCATED( Arr ) ) DEALLOCATE( Arr )
    MaxNArr = capacity
    ALLOCATE( NArr( 1, 1 ), Arr( 1, 1, capacity ) )
    NArr = 0
  END SUBROUTINE Reset

  SUBROUTINE Add( amp, phase, delayReal, srcAngle, rcvAngle, top, bottom )
    REAL( KIND = 8 ), INTENT( IN ) :: amp, phase, delayReal, srcAngle, rcvAngle
    INTEGER, INTENT( IN ) :: top, bottom
    CALL AddArr( 100.0D0 * ACOS( -1.0D0 ), 1, 1, amp, phase, &
         CMPLX( delayReal, 0.0D0, KIND = 8 ), srcAngle, rcvAngle, top, bottom )
  END SUBROUTINE Add

  SUBROUTINE RunScenario( id )
    INTEGER, INTENT( IN ) :: id
    REAL( KIND = 8 ) :: omega, phaseTol

    omega = 100.0D0 * ACOS( -1.0D0 )
    phaseTol = REAL( 0.05, KIND = 8 )
    SELECT CASE ( id )
    CASE ( 1 ) ! append
       CALL Reset( 4 ); CALL Add( 1.0D0, 0.0D0, 0.0D0, 10.0D0, 20.0D0, 1, 2 )
    CASE ( 2 ) ! last-only duplicate / weighted merge / preserved phase+bounces
       CALL Reset( 4 ); CALL Add( 1.0D0, 0.01D0, 0.0D0, 10.0D0, 20.0D0, 1, 2 )
       CALL Add( 2.0D0, 0.02D0, 0.0001D0, 40.0D0, 50.0D0, 3, 4 )
    CASE ( 3 ) ! candidate similar to first but not to last
       CALL Reset( 4 ); CALL Add( 1.0D0, 0.0D0, 0.0D0, 10.0D0, 20.0D0, 1, 2 )
       CALL Add( 2.0D0, 0.20D0, 0.01D0, 11.0D0, 21.0D0, 3, 4 )
       CALL Add( 3.0D0, 0.001D0, 0.000001D0, 12.0D0, 22.0D0, 5, 6 )
    CASE ( 4 ) ! delay strictly below PhaseTol / omega
       CALL Reset( 4 ); CALL Add( 1.0D0, 0.0D0, 0.0D0, 10.0D0, 20.0D0, 1, 2 )
       CALL Add( 2.0D0, 0.0D0, 0.049D0 / omega, 30.0D0, 40.0D0, 3, 4 )
    CASE ( 5 ) ! delay equal to threshold
       CALL Reset( 4 ); CALL Add( 1.0D0, 0.0D0, 0.0D0, 10.0D0, 20.0D0, 1, 2 )
       CALL Add( 2.0D0, 0.0D0, phaseTol / omega, 30.0D0, 40.0D0, 3, 4 )
    CASE ( 6 ) ! delay strictly above threshold
       CALL Reset( 4 ); CALL Add( 1.0D0, 0.0D0, 0.0D0, 10.0D0, 20.0D0, 1, 2 )
       CALL Add( 2.0D0, 0.0D0, 0.051D0 / omega, 30.0D0, 40.0D0, 3, 4 )
    CASE ( 7 ) ! phase strictly below threshold
       CALL Reset( 4 ); CALL Add( 1.0D0, 0.0D0, 0.0D0, 10.0D0, 20.0D0, 1, 2 )
       CALL Add( 2.0D0, 0.049D0, 0.0D0, 30.0D0, 40.0D0, 3, 4 )
    CASE ( 8 ) ! phase equal to threshold
       CALL Reset( 4 ); CALL Add( 1.0D0, 0.0D0, 0.0D0, 10.0D0, 20.0D0, 1, 2 )
       CALL Add( 2.0D0, phaseTol, 0.0D0, 30.0D0, 40.0D0, 3, 4 )
    CASE ( 9 ) ! phase strictly above threshold
       CALL Reset( 4 ); CALL Add( 1.0D0, 0.0D0, 0.0D0, 10.0D0, 20.0D0, 1, 2 )
       CALL Add( 2.0D0, 0.051D0, 0.0D0, 30.0D0, 40.0D0, 3, 4 )
    CASE ( 10 ) ! axial-cusp guard: exact zero weighted amplitude
       CALL Reset( 4 ); CALL Add( 1.0D0, 0.0D0, 0.0D0, 10.0D0, 20.0D0, 1, 2 )
       CALL Add( -1.0D0, 0.0D0, 0.0D0, 30.0D0, 40.0D0, 3, 4 )
    CASE ( 11 ) ! MINLOC selects the first equal minimum
       CALL Reset( 2 ); CALL Add( 1.0D0, 0.0D0, 0.0D0, 10.0D0, 20.0D0, 1, 2 )
       CALL Add( 1.0D0, 0.20D0, 0.01D0, 11.0D0, 21.0D0, 3, 4 )
       CALL Add( 2.0D0, 0.40D0, 0.02D0, 12.0D0, 22.0D0, 5, 6 )
    CASE ( 12 ) ! stronger candidate replaces weakest
       CALL Reset( 2 ); CALL Add( 1.0D0, 0.0D0, 0.0D0, 10.0D0, 20.0D0, 1, 2 )
       CALL Add( 2.0D0, 0.20D0, 0.01D0, 11.0D0, 21.0D0, 3, 4 )
       CALL Add( 3.0D0, 0.40D0, 0.02D0, 12.0D0, 22.0D0, 5, 6 )
    CASE ( 13 ) ! equal candidate is discarded when full
       CALL Reset( 2 ); CALL Add( 1.0D0, 0.0D0, 0.0D0, 10.0D0, 20.0D0, 1, 2 )
       CALL Add( 2.0D0, 0.20D0, 0.01D0, 11.0D0, 21.0D0, 3, 4 )
       CALL Add( 1.0D0, 0.40D0, 0.02D0, 12.0D0, 22.0D0, 5, 6 )
    CASE ( 14 ) ! weaker candidate is discarded when full
       CALL Reset( 2 ); CALL Add( 1.0D0, 0.0D0, 0.0D0, 10.0D0, 20.0D0, 1, 2 )
       CALL Add( 2.0D0, 0.20D0, 0.01D0, 11.0D0, 21.0D0, 3, 4 )
       CALL Add( 0.5D0, 0.40D0, 0.02D0, 12.0D0, 22.0D0, 5, 6 )
    CASE ( 15 ) ! zero arrivals
       CALL Reset( 4 )
    END SELECT
    CALL Emit( id )
  END SUBROUTINE RunScenario

  SUBROUTINE Emit( id )
    INTEGER, INTENT( IN ) :: id
    INTEGER :: index
    WRITE( *, '(A,1X,I0,1X,I0)' ) 'SCENARIO', id, NArr( 1, 1 )
    DO index = 1, NArr( 1, 1 )
       WRITE( *, '(A,1X,I0,1X,I0,8(1X,I0))' ) 'ARRIVAL', id, index, &
            TRANSFER( Arr( 1, 1, index )%A, 0 ), &
            TRANSFER( Arr( 1, 1, index )%Phase, 0 ), &
            TRANSFER( REAL( Arr( 1, 1, index )%delay ), 0 ), &
            TRANSFER( AIMAG( Arr( 1, 1, index )%delay ), 0 ), &
            TRANSFER( Arr( 1, 1, index )%SrcDeclAngle, 0 ), &
            TRANSFER( Arr( 1, 1, index )%RcvrDeclAngle, 0 ), &
            Arr( 1, 1, index )%NTopBnc, Arr( 1, 1, index )%NBotBnc
    END DO
  END SUBROUTINE Emit
END PROGRAM ArrivalAccumulatorProbe
