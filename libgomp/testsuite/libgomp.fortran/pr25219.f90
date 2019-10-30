! { dg-do run }
! PR fortran/25219

  implicit none
  save
  integer :: i, k
  k = 3
!$omp parallel
!$omp do lastprivate (k)
  do i = 1, 100
    k = i
  end do
!$omp end do
!$omp end parallel
  if (k .ne. 100) STOP 1
end
